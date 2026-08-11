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
 * @file wifi_connection.c
 * @brief Runtime WiFi station connection management for PSU-EXT.
 */

#include "wifi_connection.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "status_led.h"
#include "wifi_manager.h"

#define WIFI_CONNECTION_MAX_RETRIES 3
#define WIFI_CONNECTION_CONNECTED_BIT BIT0
#define WIFI_CONNECTION_FAILED_BIT BIT1

static const char *TAG = "wifi_connection";

static struct {
    bool initialized;
    bool handlers_registered;
    uint8_t retry_count;
    EventGroupHandle_t event_group;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
} s_wifi_connection;

static void wifi_connection_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        s_wifi_connection.retry_count = 0;
        status_led_set_wifi_connected(false);
        status_led_set_wifi_connecting(true);
        status_led_set_wifi_failed(false);
        ESP_LOGI(TAG, "Starting WiFi connection attempt");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        return;
    }

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;

        ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_manager_set_connection_state(false, NULL));
        status_led_set_wifi_connected(false);
        status_led_set_wifi_failed(false);

        if (s_wifi_connection.retry_count < WIFI_CONNECTION_MAX_RETRIES) {
            s_wifi_connection.retry_count++;
            status_led_set_wifi_connecting(true);
            ESP_LOGW(
                TAG,
                "WiFi disconnected (reason=%d), retry %u/%u",
                event != NULL ? event->reason : -1,
                (unsigned)s_wifi_connection.retry_count,
                (unsigned)WIFI_CONNECTION_MAX_RETRIES);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        } else {
            ESP_LOGW(
                TAG,
                "WiFi connect attempts exhausted (last reason=%d)",
                event != NULL ? event->reason : -1);
            status_led_set_wifi_connecting(false);
            status_led_set_wifi_failed(true);
            xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_FAILED_BIT);
        }
        return;
    }

    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        char ip[WIFI_MANAGER_IP_MAX_LENGTH];
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connection.retry_count = 0;
        ESP_LOGI(TAG, "WiFi connected, IP=%s", ip);
        ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_manager_set_connection_state(true, ip));
        status_led_set_wifi_connecting(false);
        status_led_set_wifi_failed(false);
        status_led_set_wifi_connected(true);
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
    }
}

esp_err_t wifi_connection_init(void)
{
    wifi_manager_state_t state;

    if (s_wifi_connection.initialized) {
        return ESP_OK;
    }

    status_led_set_wifi_connected(false);
    status_led_set_wifi_connecting(false);
    status_led_set_wifi_failed(false);

    if (!wifi_manager_credentials_are_set()) {
        ESP_LOGI(TAG, "Skipping WiFi connect on startup because SSID/password are not both set");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_get_state(&state), TAG, "reading WiFi state failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "event loop init failed");
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif != NULL, ESP_FAIL, TAG, "creating STA netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    if (s_wifi_connection.event_group == NULL) {
        s_wifi_connection.event_group = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
    }

    if (!s_wifi_connection.handlers_registered) {
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &wifi_connection_event_handler,
                NULL,
                &s_wifi_connection.wifi_event_instance),
            TAG,
            "registering WiFi event handler failed");

        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &wifi_connection_event_handler,
                NULL,
                &s_wifi_connection.ip_event_instance),
            TAG,
            "registering IP event handler failed");

        s_wifi_connection.handlers_registered = true;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };


    strlcpy((char *)wifi_config.sta.ssid, state.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, state.password, sizeof(wifi_config.sta.password));

    // ESP_LOGD(TAG, "Connecting to WiFi PWD '%s'", wifi_config.sta.password);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");

    xEventGroupClearBits(
        s_wifi_connection.event_group,
        WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_FAILED_BIT);

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_connection.event_group,
        WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if ((bits & WIFI_CONNECTION_FAILED_BIT) != 0) {
        ESP_LOGW(TAG, "Unable to connect to SSID '%s' after %u attempts", state.ssid, WIFI_CONNECTION_MAX_RETRIES);
        status_led_set_wifi_connected(false);
        status_led_set_wifi_connecting(false);
        status_led_set_wifi_failed(true);
    }

    s_wifi_connection.initialized = true;
    return ESP_OK;
}
