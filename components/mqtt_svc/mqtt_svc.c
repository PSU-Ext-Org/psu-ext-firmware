/*
 * Copyright 2026 PSU-EXT Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file mqtt_svc.c
 * @brief MQTT connection lifecycle, Home Assistant publishing, and relay control.
 *
 * All publishing and output control runs in one worker task. Listeners and the
 * esp_mqtt event handler only record data and notify the worker, because the
 * output_ctrl listener can be called with the protection lock held.
 */

#include "mqtt_svc.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "measure_svc_samples.h"
#include "mqtt_client.h"
#include "mqtt_svc_command.h"
#include "mqtt_svc_config.h"
#include "mqtt_svc_payload.h"
#include "mqtt_svc_topics.h"
#include "mqtt_svc_window.h"
#include "output_ctrl.h"
#include "protection_svc.h"
#include "wifi_manager.h"

#define MQTT_SVC_TASK_STACK_SIZE 6144U
#define MQTT_SVC_TASK_PRIORITY 4U
#define MQTT_SVC_KEEPALIVE_S 30
#define MQTT_SVC_RECONNECT_MS 5000
#define MQTT_SVC_RX_BUFFER_SIZE 1024
#define MQTT_SVC_TX_BUFFER_SIZE 6144

#define MQTT_SVC_NOTIFY_RECONFIGURE (1UL << 0)
#define MQTT_SVC_NOTIFY_NETWORK (1UL << 1)
#define MQTT_SVC_NOTIFY_CONNECTED (1UL << 2)
#define MQTT_SVC_NOTIFY_DISCONNECTED (1UL << 3)
#define MQTT_SVC_NOTIFY_OUTPUT (1UL << 4)
#define MQTT_SVC_NOTIFY_RELAY_COMMAND (1UL << 5)
#define MQTT_SVC_NOTIFY_HA_ONLINE (1UL << 6)
#define MQTT_SVC_NOTIFY_WAKE (1UL << 7)

static const char *TAG = "mqtt_svc";

static struct {
    bool initialized;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    mqtt_svc_config_t config;
    mqtt_svc_topics_t topics;
    uint8_t sta_mac[6];
    char last_error[MQTT_SVC_ERROR_MAX_LENGTH];
    bool client_running;
    volatile bool connected;
    volatile bool wifi_connected;
    portMUX_TYPE pending_lock;
    mqtt_svc_window_t window;
    output_ctrl_change_cause_t pending_cause;
    bool pending_cause_valid;
    mqtt_svc_command_t pending_command;
} s_mqtt = {
    .pending_lock = portMUX_INITIALIZER_UNLOCKED,
};

/* Result of the last mqtt_svc_init() attempt, reported by get_status. */
static esp_err_t s_init_error = ESP_ERR_INVALID_STATE;

/* Worker-task-only state. */
static esp_mqtt_client_handle_t s_client;
static const char *s_last_cause = MQTT_SVC_CAUSE_NONE;
static mqtt_svc_protection_t s_published_protection;
static bool s_protection_published;
static int s_published_relay = -1;

static void mqtt_svc_notify(uint32_t bits)
{
    if (s_mqtt.task != NULL) {
        xTaskNotify(s_mqtt.task, bits, eSetBits);
    }
}

static esp_err_t mqtt_svc_take_lock(void)
{
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    return xSemaphoreTake(s_mqtt.lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void mqtt_svc_set_last_error(const char *error)
{
    if (xSemaphoreTake(s_mqtt.lock, portMAX_DELAY) == pdTRUE) {
        strlcpy(s_mqtt.last_error, error != NULL ? error : "", sizeof(s_mqtt.last_error));
        xSemaphoreGive(s_mqtt.lock);
    }
}

/* ---- Listeners (run in foreign task contexts) ---- */

static void mqtt_svc_sample_listener(const measure_svc_sample_event_t *event, void *context)
{
    (void)context;
    if ((event == NULL) || (event->channel != MEASURE_CHANNEL_1)) {
        return;
    }

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    mqtt_svc_window_add(&s_mqtt.window, event->kind, event->value_u4);
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);
}

static void mqtt_svc_output_listener(const output_ctrl_change_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    s_mqtt.pending_cause = event->cause;
    s_mqtt.pending_cause_valid = true;
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);
    mqtt_svc_notify(MQTT_SVC_NOTIFY_OUTPUT);
}

static void mqtt_svc_network_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_data;

    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        s_mqtt.wifi_connected = true;
        mqtt_svc_notify(MQTT_SVC_NOTIFY_NETWORK);
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        s_mqtt.wifi_connected = false;
        mqtt_svc_notify(MQTT_SVC_NOTIFY_NETWORK);
    }
}

static bool mqtt_svc_topic_equals(const char *topic, int topic_length, const char *expected)
{
    const size_t expected_length = strlen(expected);
    return (topic != NULL) && (topic_length >= 0) &&
           ((size_t)topic_length == expected_length) &&
           (memcmp(topic, expected, expected_length) == 0);
}

static void mqtt_svc_record_error(const esp_mqtt_error_codes_t *error)
{
    char text[MQTT_SVC_ERROR_MAX_LENGTH];

    if (error == NULL) {
        return;
    }

    if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        switch (error->connect_return_code) {
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
            strlcpy(text, "auth failed", sizeof(text));
            break;
        case MQTT_CONNECTION_REFUSE_ID_REJECTED:
            strlcpy(text, "client id rejected", sizeof(text));
            break;
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            strlcpy(text, "server unavailable", sizeof(text));
            break;
        default:
            strlcpy(text, "connection refused", sizeof(text));
            break;
        }
    } else if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        if (error->esp_tls_last_esp_err != ESP_OK) {
            snprintf(text, sizeof(text), "transport: %s", esp_err_to_name(error->esp_tls_last_esp_err));
        } else if (error->esp_transport_sock_errno != 0) {
            snprintf(text, sizeof(text), "socket errno %d", error->esp_transport_sock_errno);
        } else {
            strlcpy(text, "transport error", sizeof(text));
        }
    } else {
        strlcpy(text, "mqtt error", sizeof(text));
    }

    ESP_LOGW(TAG, "MQTT error: %s", text);
    mqtt_svc_set_last_error(text);
}

static void mqtt_svc_mqtt_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)arg;
    (void)event_base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        mqtt_svc_notify(MQTT_SVC_NOTIFY_CONNECTED);
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_svc_notify(MQTT_SVC_NOTIFY_DISCONNECTED);
        break;
    case MQTT_EVENT_ERROR:
        mqtt_svc_record_error(event->error_handle);
        break;
    case MQTT_EVENT_DATA:
        if ((event->current_data_offset != 0) || (event->data_len != event->total_data_len)) {
            break;
        }

        if (mqtt_svc_topic_equals(event->topic, event->topic_len, s_mqtt.topics.relay_set)) {
            const mqtt_svc_command_t command =
                mqtt_svc_command_parse_relay(event->data, (size_t)event->data_len, event->retain);
            if (command == MQTT_SVC_COMMAND_RETAINED) {
                ESP_LOGW(TAG, "Dropping retained relay/set message");
            } else if (command == MQTT_SVC_COMMAND_IGNORE) {
                ESP_LOGW(TAG, "Ignoring relay/set payload of %d bytes", event->data_len);
            } else {
                taskENTER_CRITICAL(&s_mqtt.pending_lock);
                s_mqtt.pending_command = command;
                taskEXIT_CRITICAL(&s_mqtt.pending_lock);
                mqtt_svc_notify(MQTT_SVC_NOTIFY_RELAY_COMMAND);
            }
        } else if (mqtt_svc_topic_equals(event->topic, event->topic_len, s_mqtt.topics.ha_status) &&
                   mqtt_svc_command_is_ha_online(event->data, (size_t)event->data_len)) {
            mqtt_svc_notify(MQTT_SVC_NOTIFY_HA_ONLINE);
        }
        break;
    default:
        break;
    }
}

/* ---- Worker task ---- */

static void mqtt_svc_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if ((s_client == NULL) || (payload == NULL)) {
        return;
    }

    if (esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain ? 1 : 0) < 0) {
        ESP_LOGW(TAG, "Publishing %s failed", topic);
    }
}

static void mqtt_svc_publish_relay_state(bool force)
{
    bool enabled = false;

    if (!s_mqtt.connected || (output_ctrl_get(PROTECTION_SVC_CHANNEL_CH1, &enabled) != ESP_OK)) {
        return;
    }

    if (!force && (s_published_relay == (enabled ? 1 : 0))) {
        return;
    }

    mqtt_svc_publish(s_mqtt.topics.relay_state, enabled ? "ON" : "OFF", 1, true);
    s_published_relay = enabled ? 1 : 0;
}

static bool mqtt_svc_read_protection(mqtt_svc_protection_t *protection)
{
    esp_err_t err;

    memset(protection, 0, sizeof(*protection));
    err = protection_svc_get_ovp_tripped(PROTECTION_SVC_CHANNEL_CH0, &protection->input_ovp);
    if (err == ESP_OK) {
        err = protection_svc_get_ovp_tripped(PROTECTION_SVC_CHANNEL_CH1, &protection->output_ovp);
    }
    if (err == ESP_OK) {
        err = protection_svc_get_ocp_tripped(PROTECTION_SVC_CHANNEL_CH1, &protection->ocp);
    }
    if (err == ESP_OK) {
        err = protection_svc_get_ovp_u4(PROTECTION_SVC_CHANNEL_CH1, &protection->ovp_threshold_u4);
    }
    if (err == ESP_OK) {
        err = protection_svc_get_ocp_u4(PROTECTION_SVC_CHANNEL_CH1, &protection->ocp_threshold_u4);
    }
    protection->cause = s_last_cause;
    return err == ESP_OK;
}

static void mqtt_svc_publish_protection(bool force)
{
    mqtt_svc_protection_t protection;
    char *payload;

    if (!s_mqtt.connected || !mqtt_svc_read_protection(&protection)) {
        return;
    }

    if (!force && s_protection_published &&
        mqtt_svc_payload_protection_equal(&protection, &s_published_protection)) {
        return;
    }

    payload = mqtt_svc_payload_protection(&protection);
    if (payload == NULL) {
        ESP_LOGW(TAG, "Building protection payload failed");
        return;
    }

    mqtt_svc_publish(s_mqtt.topics.protection_state, payload, 1, true);
    mqtt_svc_payload_free(payload);
    s_published_protection = protection;
    s_protection_published = true;
}

static void mqtt_svc_publish_discovery(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char *payload = mqtt_svc_payload_discovery(&s_mqtt.topics, app->version);

    if (payload == NULL) {
        ESP_LOGW(TAG, "Building discovery payload failed");
        return;
    }

    mqtt_svc_publish(s_mqtt.topics.discovery, payload, 1, true);
    mqtt_svc_payload_free(payload);
    mqtt_svc_publish(s_mqtt.topics.availability, "online", 1, true);
}

static void mqtt_svc_publish_telemetry(void)
{
    mqtt_svc_window_t window;
    mqtt_svc_telemetry_t telemetry;
    bool enabled = false;
    char *payload;

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    window = s_mqtt.window;
    mqtt_svc_window_reset(&s_mqtt.window);
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);

    if (!s_mqtt.connected || !mqtt_svc_window_summarize(&window, &telemetry)) {
        return;
    }

    (void)output_ctrl_get(PROTECTION_SVC_CHANNEL_CH1, &enabled);
    payload = mqtt_svc_payload_telemetry(&telemetry, enabled);
    if (payload == NULL) {
        ESP_LOGW(TAG, "Building telemetry payload failed");
        return;
    }

    mqtt_svc_publish(s_mqtt.topics.measurements, payload, 0, false);
    mqtt_svc_payload_free(payload);
}

static void mqtt_svc_on_connected(void)
{
    s_mqtt.connected = true;
    mqtt_svc_set_last_error("");
    ESP_LOGI(TAG, "Connected as %s", s_mqtt.topics.client_id);

    if (esp_mqtt_client_subscribe(s_client, s_mqtt.topics.relay_set, 0) < 0) {
        ESP_LOGW(TAG, "Subscribing to relay/set failed");
    }
    if (esp_mqtt_client_subscribe(s_client, s_mqtt.topics.ha_status, 0) < 0) {
        ESP_LOGW(TAG, "Subscribing to HA status failed");
    }

    mqtt_svc_publish_discovery();
    mqtt_svc_publish_relay_state(true);
    mqtt_svc_publish_protection(true);

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    mqtt_svc_window_reset(&s_mqtt.window);
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);
}

static void mqtt_svc_stop_client(void)
{
    if (s_client == NULL) {
        return;
    }

    if (s_mqtt.connected) {
        /* A clean disconnect suppresses the LWT, so mark offline explicitly. */
        mqtt_svc_publish(s_mqtt.topics.availability, "offline", 1, true);
    }

    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_mqtt.connected = false;
    s_mqtt.client_running = false;
    s_protection_published = false;
    s_published_relay = -1;
}

static void mqtt_svc_start_client(const mqtt_svc_config_t *config)
{
    esp_mqtt_client_config_t client_config = {0};
    mqtt_svc_topics_t topics;
    esp_err_t err;

    err = mqtt_svc_topics_build(s_mqtt.sta_mac, config->prefix, &topics);
    if (err != ESP_OK) {
        mqtt_svc_set_last_error("topic too long");
        return;
    }

    if (xSemaphoreTake(s_mqtt.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_mqtt.topics = topics;
    xSemaphoreGive(s_mqtt.lock);

    client_config.broker.address.uri = config->uri;
    if (strncmp(config->uri, "mqtts://", 8) == 0) {
        client_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    client_config.credentials.client_id = s_mqtt.topics.client_id;
    client_config.credentials.username = config->user[0] != '\0' ? config->user : NULL;
    client_config.credentials.authentication.password = config->password[0] != '\0' ? config->password : NULL;
    client_config.session.keepalive = MQTT_SVC_KEEPALIVE_S;
    client_config.session.disable_clean_session = false;
    client_config.session.last_will.topic = s_mqtt.topics.availability;
    client_config.session.last_will.msg = "offline";
    client_config.session.last_will.msg_len = 7;
    client_config.session.last_will.qos = 1;
    client_config.session.last_will.retain = 1;
    client_config.network.reconnect_timeout_ms = MQTT_SVC_RECONNECT_MS;
    client_config.buffer.size = MQTT_SVC_RX_BUFFER_SIZE;
    client_config.buffer.out_size = MQTT_SVC_TX_BUFFER_SIZE;

    s_client = esp_mqtt_client_init(&client_config);
    if (s_client == NULL) {
        mqtt_svc_set_last_error("client init failed");
        return;
    }

    err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_svc_mqtt_event_handler, NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_client);
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        mqtt_svc_set_last_error("client start failed");
        return;
    }

    s_mqtt.client_running = true;
    ESP_LOGI(TAG, "Connecting to %s", config->uri);
}

static void mqtt_svc_reconcile(bool restart)
{
    mqtt_svc_config_t config;
    bool desired;

    if (xSemaphoreTake(s_mqtt.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    config = s_mqtt.config;
    xSemaphoreGive(s_mqtt.lock);

    desired = config.enabled && (config.uri[0] != '\0') && s_mqtt.wifi_connected;
    if ((s_client != NULL) && (restart || !desired)) {
        mqtt_svc_stop_client();
    }

    if ((s_client == NULL) && desired) {
        mqtt_svc_set_last_error("");
        mqtt_svc_start_client(&config);
    }

    memset(&config, 0, sizeof(config));
}

static void mqtt_svc_handle_relay_command(void)
{
    mqtt_svc_command_t command;
    esp_err_t err;

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    command = s_mqtt.pending_command;
    s_mqtt.pending_command = MQTT_SVC_COMMAND_IGNORE;
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);

    if ((command != MQTT_SVC_COMMAND_ON) && (command != MQTT_SVC_COMMAND_OFF)) {
        return;
    }

    err = protection_svc_set_ch1_output(command == MQTT_SVC_COMMAND_ON, OUTPUT_CTRL_CHANGE_CAUSE_MQTT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "relay/set %s refused: %s", command == MQTT_SVC_COMMAND_ON ? "ON" : "OFF", esp_err_to_name(err));
        /* Re-assert the unchanged state so a non-optimistic HA switch snaps back. */
        mqtt_svc_publish_relay_state(true);
        mqtt_svc_publish_protection(false);
    }
}

static void mqtt_svc_handle_output_change(void)
{
    output_ctrl_change_cause_t cause;
    bool cause_valid;

    taskENTER_CRITICAL(&s_mqtt.pending_lock);
    cause = s_mqtt.pending_cause;
    cause_valid = s_mqtt.pending_cause_valid;
    s_mqtt.pending_cause_valid = false;
    taskEXIT_CRITICAL(&s_mqtt.pending_lock);

    if (cause_valid) {
        s_last_cause = mqtt_svc_payload_cause_name(cause);
    }

    mqtt_svc_publish_relay_state(false);
    mqtt_svc_publish_protection(false);
}

static void mqtt_svc_task(void *arg)
{
    int64_t next_tick_us = esp_timer_get_time();
    (void)arg;

    mqtt_svc_reconcile(false);

    for (;;) {
        uint32_t bits = 0U;
        uint32_t interval_ms;
        TickType_t wait_ticks = portMAX_DELAY;

        if (xSemaphoreTake(s_mqtt.lock, portMAX_DELAY) == pdTRUE) {
            interval_ms = s_mqtt.config.interval_ms;
            xSemaphoreGive(s_mqtt.lock);
        } else {
            interval_ms = MQTT_SVC_DEFAULT_INTERVAL_MS;
        }

        if (s_mqtt.connected) {
            const int64_t now_us = esp_timer_get_time();
            const int64_t max_wait_us = (int64_t)interval_ms * 1000LL;
            if (next_tick_us > now_us + max_wait_us) {
                next_tick_us = now_us + max_wait_us;
            }
            wait_ticks = next_tick_us > now_us ? pdMS_TO_TICKS((next_tick_us - now_us) / 1000LL) : 0;
        }

        xTaskNotifyWait(0U, UINT32_MAX, &bits, wait_ticks);

        if ((bits & MQTT_SVC_NOTIFY_RECONFIGURE) != 0U) {
            mqtt_svc_reconcile(true);
        } else if ((bits & MQTT_SVC_NOTIFY_NETWORK) != 0U) {
            mqtt_svc_reconcile(false);
        }

        if ((bits & MQTT_SVC_NOTIFY_DISCONNECTED) != 0U) {
            s_mqtt.connected = false;
            s_protection_published = false;
            s_published_relay = -1;
            ESP_LOGW(TAG, "Disconnected from broker");
        }

        if (((bits & MQTT_SVC_NOTIFY_CONNECTED) != 0U) && (s_client != NULL)) {
            mqtt_svc_on_connected();
            next_tick_us = esp_timer_get_time() + ((int64_t)interval_ms * 1000LL);
        }

        if ((bits & MQTT_SVC_NOTIFY_HA_ONLINE) != 0U && s_mqtt.connected) {
            ESP_LOGI(TAG, "Home Assistant online, republishing discovery");
            mqtt_svc_publish_discovery();
            mqtt_svc_publish_relay_state(true);
            mqtt_svc_publish_protection(true);
        }

        if ((bits & MQTT_SVC_NOTIFY_RELAY_COMMAND) != 0U && s_mqtt.connected) {
            mqtt_svc_handle_relay_command();
        }

        if ((bits & MQTT_SVC_NOTIFY_OUTPUT) != 0U) {
            mqtt_svc_handle_output_change();
        }

        if (s_mqtt.connected && (esp_timer_get_time() >= next_tick_us)) {
            mqtt_svc_publish_telemetry();
            mqtt_svc_publish_protection(false);
            next_tick_us += (int64_t)interval_ms * 1000LL;
            if (next_tick_us <= esp_timer_get_time()) {
                next_tick_us = esp_timer_get_time() + ((int64_t)interval_ms * 1000LL);
            }
        }
    }
}

/* ---- Public API ---- */

static esp_err_t mqtt_svc_init_internal(void)
{
    wifi_manager_state_t wifi_state;
    esp_err_t err;

    if (s_mqtt.lock == NULL) {
        s_mqtt.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mqtt.lock != NULL, ESP_ERR_NO_MEM, TAG, "lock alloc failed");
    }

    ESP_RETURN_ON_ERROR(mqtt_svc_config_load(&s_mqtt.config), TAG, "loading config failed");
    ESP_RETURN_ON_ERROR(esp_read_mac(s_mqtt.sta_mac, ESP_MAC_WIFI_STA), TAG, "reading STA MAC failed");
    ESP_RETURN_ON_ERROR(
        mqtt_svc_topics_build(s_mqtt.sta_mac, s_mqtt.config.prefix, &s_mqtt.topics),
        TAG,
        "building topics failed");
    mqtt_svc_window_reset(&s_mqtt.window);

    err = wifi_manager_get_state(&wifi_state);
    s_mqtt.wifi_connected = (err == ESP_OK) && wifi_state.is_connected;
    memset(&wifi_state, 0, sizeof(wifi_state));

    /* wifi_connection skips creating the loop when no credentials are stored. */
    err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(
        (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE),
        err,
        TAG,
        "creating default event loop failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, mqtt_svc_network_event_handler, NULL),
        TAG,
        "registering IP handler failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, mqtt_svc_network_event_handler, NULL),
        TAG,
        "registering Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(output_ctrl_register_listener(mqtt_svc_output_listener, NULL), TAG, "registering output listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(MEASURE_KIND_VOLTAGE, mqtt_svc_sample_listener, NULL),
        TAG,
        "registering voltage listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(MEASURE_KIND_CURRENT, mqtt_svc_sample_listener, NULL),
        TAG,
        "registering current listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(MEASURE_KIND_POWER, mqtt_svc_sample_listener, NULL),
        TAG,
        "registering power listener failed");

    s_mqtt.initialized = true;

    if (xTaskCreate(
            mqtt_svc_task,
            "mqtt_svc",
            MQTT_SVC_TASK_STACK_SIZE,
            NULL,
            MQTT_SVC_TASK_PRIORITY,
            &s_mqtt.task) != pdPASS) {
        s_mqtt.initialized = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "MQTT service initialized (%s, id %s)",
        s_mqtt.config.enabled ? "enabled" : "disabled",
        s_mqtt.topics.device_id);
    return ESP_OK;
}

esp_err_t mqtt_svc_init(void)
{
    if (s_mqtt.initialized) {
        return ESP_OK;
    }

    s_init_error = mqtt_svc_init_internal();
    if (s_init_error != ESP_OK) {
        ESP_LOGE(TAG, "MQTT service init failed: %s", esp_err_to_name(s_init_error));
    }
    return s_init_error;
}

static esp_err_t mqtt_svc_store_string_setting(
    const char *key,
    const char *value,
    char *destination,
    size_t destination_size,
    uint32_t notify_bits)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    esp_err_t err = mqtt_svc_config_store_string(key, value);
    if (err == ESP_OK) {
        strlcpy(destination, value, destination_size);
    }
    xSemaphoreGive(s_mqtt.lock);

    if (err == ESP_OK) {
        mqtt_svc_notify(notify_bits);
    }
    return err;
}

static esp_err_t mqtt_svc_copy_string_setting(const char *source, char *destination, size_t destination_size)
{
    ESP_RETURN_ON_FALSE(destination != NULL, ESP_ERR_INVALID_ARG, TAG, "destination is null");
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    const size_t length = strlcpy(destination, source, destination_size);
    xSemaphoreGive(s_mqtt.lock);
    return length < destination_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t mqtt_svc_set_uri(const char *uri)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_config_validate_uri(uri), TAG, "invalid URI");
    return mqtt_svc_store_string_setting(
        MQTT_SVC_CONFIG_KEY_URI,
        uri,
        s_mqtt.config.uri,
        sizeof(s_mqtt.config.uri),
        MQTT_SVC_NOTIFY_RECONFIGURE);
}

esp_err_t mqtt_svc_get_uri(char *uri, size_t uri_size)
{
    return mqtt_svc_copy_string_setting(s_mqtt.config.uri, uri, uri_size);
}

esp_err_t mqtt_svc_set_user(const char *user)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_config_validate_credential(user, MQTT_SVC_USER_MAX_LENGTH), TAG, "invalid user");
    return mqtt_svc_store_string_setting(
        MQTT_SVC_CONFIG_KEY_USER,
        user,
        s_mqtt.config.user,
        sizeof(s_mqtt.config.user),
        MQTT_SVC_NOTIFY_RECONFIGURE);
}

esp_err_t mqtt_svc_get_user(char *user, size_t user_size)
{
    return mqtt_svc_copy_string_setting(s_mqtt.config.user, user, user_size);
}

esp_err_t mqtt_svc_set_password(const char *password)
{
    ESP_RETURN_ON_ERROR(
        mqtt_svc_config_validate_credential(password, MQTT_SVC_PASSWORD_MAX_LENGTH),
        TAG,
        "invalid password");
    return mqtt_svc_store_string_setting(
        MQTT_SVC_CONFIG_KEY_PASSWORD,
        password,
        s_mqtt.config.password,
        sizeof(s_mqtt.config.password),
        MQTT_SVC_NOTIFY_RECONFIGURE);
}

bool mqtt_svc_password_is_set(void)
{
    bool is_set = false;

    if (mqtt_svc_take_lock() == ESP_OK) {
        is_set = s_mqtt.config.password[0] != '\0';
        xSemaphoreGive(s_mqtt.lock);
    }
    return is_set;
}

esp_err_t mqtt_svc_set_enabled(bool enabled)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    esp_err_t err = mqtt_svc_config_store_u32(MQTT_SVC_CONFIG_KEY_ENABLED, enabled ? 1U : 0U);
    if (err == ESP_OK) {
        s_mqtt.config.enabled = enabled;
    }
    xSemaphoreGive(s_mqtt.lock);

    if (err == ESP_OK) {
        mqtt_svc_notify(MQTT_SVC_NOTIFY_RECONFIGURE);
    }
    return err;
}

esp_err_t mqtt_svc_get_enabled(bool *enabled)
{
    ESP_RETURN_ON_FALSE(enabled != NULL, ESP_ERR_INVALID_ARG, TAG, "enabled pointer is null");
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    *enabled = s_mqtt.config.enabled;
    xSemaphoreGive(s_mqtt.lock);
    return ESP_OK;
}

esp_err_t mqtt_svc_set_interval_ms(uint32_t interval_ms)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_config_validate_interval(interval_ms), TAG, "invalid interval");
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    esp_err_t err = mqtt_svc_config_store_u32(MQTT_SVC_CONFIG_KEY_INTERVAL, interval_ms);
    if (err == ESP_OK) {
        s_mqtt.config.interval_ms = interval_ms;
    }
    xSemaphoreGive(s_mqtt.lock);

    if (err == ESP_OK) {
        mqtt_svc_notify(MQTT_SVC_NOTIFY_WAKE);
    }
    return err;
}

esp_err_t mqtt_svc_get_interval_ms(uint32_t *interval_ms)
{
    ESP_RETURN_ON_FALSE(interval_ms != NULL, ESP_ERR_INVALID_ARG, TAG, "interval pointer is null");
    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");
    *interval_ms = s_mqtt.config.interval_ms;
    xSemaphoreGive(s_mqtt.lock);
    return ESP_OK;
}

esp_err_t mqtt_svc_set_prefix(const char *prefix)
{
    ESP_RETURN_ON_ERROR(mqtt_svc_config_validate_prefix(prefix), TAG, "invalid prefix");
    return mqtt_svc_store_string_setting(
        MQTT_SVC_CONFIG_KEY_PREFIX,
        prefix,
        s_mqtt.config.prefix,
        sizeof(s_mqtt.config.prefix),
        MQTT_SVC_NOTIFY_RECONFIGURE);
}

esp_err_t mqtt_svc_get_prefix(char *prefix, size_t prefix_size)
{
    return mqtt_svc_copy_string_setting(s_mqtt.config.prefix, prefix, prefix_size);
}

esp_err_t mqtt_svc_get_status(mqtt_svc_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status pointer is null");

    if (!s_mqtt.initialized) {
        memset(status, 0, sizeof(*status));
        status->state = MQTT_SVC_STATE_ERROR;
        snprintf(status->last_error, sizeof(status->last_error), "init failed: %s", esp_err_to_name(s_init_error));
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mqtt_svc_take_lock(), TAG, "taking lock failed");

    memset(status, 0, sizeof(*status));
    strlcpy(status->client_id, s_mqtt.topics.client_id, sizeof(status->client_id));
    strlcpy(status->last_error, s_mqtt.last_error, sizeof(status->last_error));

    if (!s_mqtt.config.enabled) {
        status->state = MQTT_SVC_STATE_DISABLED;
    } else if (s_mqtt.config.uri[0] == '\0') {
        status->state = MQTT_SVC_STATE_ERROR;
        strlcpy(status->last_error, "no broker URI", sizeof(status->last_error));
    } else if (!s_mqtt.wifi_connected) {
        status->state = MQTT_SVC_STATE_WAIT_WIFI;
    } else if (s_mqtt.connected) {
        status->state = MQTT_SVC_STATE_CONNECTED;
    } else if (s_mqtt.last_error[0] != '\0') {
        status->state = MQTT_SVC_STATE_ERROR;
    } else {
        status->state = MQTT_SVC_STATE_CONNECTING;
    }

    xSemaphoreGive(s_mqtt.lock);
    return ESP_OK;
}

const char *mqtt_svc_state_name(mqtt_svc_state_t state)
{
    switch (state) {
    case MQTT_SVC_STATE_DISABLED:
        return "DISABLED";
    case MQTT_SVC_STATE_WAIT_WIFI:
        return "WAIT_WIFI";
    case MQTT_SVC_STATE_CONNECTING:
        return "CONNECTING";
    case MQTT_SVC_STATE_CONNECTED:
        return "CONNECTED";
    case MQTT_SVC_STATE_ERROR:
    default:
        return "ERROR";
    }
}
