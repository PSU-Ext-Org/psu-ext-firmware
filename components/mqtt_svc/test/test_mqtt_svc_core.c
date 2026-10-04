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

#include "../mqtt_svc_command.h"
#include "../mqtt_svc_config.h"
#include "../mqtt_svc_format.h"
#include "../mqtt_svc_payload.h"
#include "../mqtt_svc_topics.h"
#include "../mqtt_svc_window.h"
#include "unity.h"

#include <string.h>

static size_t count_occurrences(const char *haystack, const char *needle)
{
    size_t count = 0U;
    const size_t needle_length = strlen(needle);

    for (const char *cursor = strstr(haystack, needle); cursor != NULL; cursor = strstr(cursor + needle_length, needle)) {
        count++;
    }
    return count;
}

static void build_test_topics(mqtt_svc_topics_t *topics)
{
    static const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xa1, 0xb2, 0xc3};
    TEST_ESP_OK(mqtt_svc_topics_build(mac, "homeassistant", topics));
}

TEST_CASE("mqtt u4 formatter prints four decimals without floats", "[mqtt]")
{
    char text[MQTT_SVC_FORMAT_U4_SIZE];

    TEST_ASSERT_TRUE(mqtt_svc_format_u4(0U, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("0.0000", text);
    TEST_ASSERT_TRUE(mqtt_svc_format_u4(120431U, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("12.0431", text);
    TEST_ASSERT_TRUE(mqtt_svc_format_u4(5U, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("0.0005", text);
    TEST_ASSERT_TRUE(mqtt_svc_format_u4(UINT32_MAX, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("429496.7295", text);

    char small[6];
    TEST_ASSERT_FALSE(mqtt_svc_format_u4(120431U, small, sizeof(small)));
}

TEST_CASE("mqtt window catches a current burst the mean hides", "[mqtt]")
{
    mqtt_svc_window_t window;
    mqtt_svc_telemetry_t telemetry;

    /* 1 s at 100 Hz: 80 mA idle with a 550 mA burst lasting 50 ms. */
    mqtt_svc_window_reset(&window);
    for (int sample = 0; sample < 100; ++sample) {
        const bool burst = (sample >= 40) && (sample < 45);
        const uint32_t current_u4 = burst ? 5500U : 800U;
        const uint32_t voltage_u4 = burst ? 118120U : 120000U;
        mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, voltage_u4);
        mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, current_u4);
        mqtt_svc_window_add(&window, MEASURE_KIND_POWER, (uint32_t)(((uint64_t)voltage_u4 * current_u4 + 5000U) / 10000U));
    }

    TEST_ASSERT_TRUE(mqtt_svc_window_summarize(&window, &telemetry));
    TEST_ASSERT_EQUAL_UINT32(5500U, telemetry.current_max_u4);
    TEST_ASSERT_EQUAL_UINT32(1035U, telemetry.current_u4);
    TEST_ASSERT_EQUAL_UINT32(118120U, telemetry.voltage_min_u4);
    TEST_ASSERT_EQUAL_UINT32(120000U, telemetry.voltage_max_u4);
    TEST_ASSERT_EQUAL_UINT32(119906U, telemetry.voltage_u4);
    /* Mean of per-sample V*I: (95 * 9600 + 5 * 64966) / 100 = 12368.3 -> 12368. */
    TEST_ASSERT_EQUAL_UINT32(12368U, telemetry.power_u4);
}

TEST_CASE("mqtt window rounds means and requires voltage and current", "[mqtt]")
{
    mqtt_svc_window_t window;
    mqtt_svc_telemetry_t telemetry;

    mqtt_svc_window_reset(&window);
    TEST_ASSERT_FALSE(mqtt_svc_window_summarize(&window, &telemetry));

    mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, 1U);
    mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, 2U);
    TEST_ASSERT_FALSE(mqtt_svc_window_summarize(&window, &telemetry));

    mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, 7U);
    TEST_ASSERT_TRUE(mqtt_svc_window_summarize(&window, &telemetry));
    TEST_ASSERT_EQUAL_UINT32(2U, telemetry.voltage_u4);
    TEST_ASSERT_EQUAL_UINT32(1U, telemetry.voltage_min_u4);
    TEST_ASSERT_EQUAL_UINT32(7U, telemetry.current_u4);
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.power_u4);

    mqtt_svc_window_reset(&window);
    TEST_ASSERT_FALSE(mqtt_svc_window_summarize(&window, &telemetry));
}

TEST_CASE("mqtt telemetry and protection payloads match the documented format", "[mqtt]")
{
    const mqtt_svc_telemetry_t telemetry = {
        .voltage_u4 = 120431U,
        .voltage_min_u4 = 118120U,
        .voltage_max_u4 = 120610U,
        .current_u4 = 5240U,
        .current_max_u4 = 6012U,
        .power_u4 = 63110U,
    };
    char *payload = mqtt_svc_payload_telemetry(&telemetry, true);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_STRING(
        "{\"voltage\":12.0431,\"voltage_min\":11.8120,\"voltage_max\":12.0610,"
        "\"current\":0.5240,\"current_max\":0.6012,\"power\":6.3110,\"output\":\"ON\"}",
        payload);
    mqtt_svc_payload_free(payload);

    const mqtt_svc_protection_t protection = {
        .input_ovp = false,
        .output_ovp = false,
        .ocp = true,
        .ovp_threshold_u4 = 250000U,
        .ocp_threshold_u4 = 25000U,
        .cause = mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP),
    };
    payload = mqtt_svc_payload_protection(&protection);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_STRING(
        "{\"input_ovp\":\"OFF\",\"output_ovp\":\"OFF\",\"ocp\":\"ON\","
        "\"ovp_threshold\":25.0000,\"ocp_threshold\":2.5000,\"cause\":\"ocp\"}",
        payload);
    mqtt_svc_payload_free(payload);

    mqtt_svc_protection_t changed = protection;
    TEST_ASSERT_TRUE(mqtt_svc_payload_protection_equal(&protection, &changed));
    changed.cause = mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_MQTT);
    TEST_ASSERT_FALSE(mqtt_svc_payload_protection_equal(&protection, &changed));
    changed = protection;
    changed.input_ovp = true;
    TEST_ASSERT_FALSE(mqtt_svc_payload_protection_equal(&protection, &changed));
}

TEST_CASE("mqtt cause names cover every output change cause", "[mqtt]")
{
    TEST_ASSERT_EQUAL_STRING("scpi", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_SCPI));
    TEST_ASSERT_EQUAL_STRING("mqtt", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_MQTT));
    TEST_ASSERT_EQUAL_STRING("trigger", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_TRIGGER));
    TEST_ASSERT_EQUAL_STRING("ovp", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OVP));
    TEST_ASSERT_EQUAL_STRING("ocp", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP));
    TEST_ASSERT_EQUAL_STRING("timer", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_TIMER_EXPIRY));
    TEST_ASSERT_EQUAL_STRING("timer", mqtt_svc_payload_cause_name(OUTPUT_CTRL_CHANGE_CAUSE_TIMER_CLEAR));
}

TEST_CASE("mqtt relay command filter accepts only exact live ON and OFF", "[mqtt]")
{
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_ON, mqtt_svc_command_parse_relay("ON", 2U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_OFF, mqtt_svc_command_parse_relay("OFF", 3U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_RETAINED, mqtt_svc_command_parse_relay("ON", 2U, true));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay("on", 2U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay("ON\n", 3U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay("ONX", 3U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay("1", 1U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay("", 0U, false));
    TEST_ASSERT_EQUAL(MQTT_SVC_COMMAND_IGNORE, mqtt_svc_command_parse_relay(NULL, 0U, false));

    TEST_ASSERT_TRUE(mqtt_svc_command_is_ha_online("online", 6U));
    TEST_ASSERT_FALSE(mqtt_svc_command_is_ha_online("offline", 7U));
    TEST_ASSERT_FALSE(mqtt_svc_command_is_ha_online("online ", 7U));
}

TEST_CASE("mqtt config validation rejects bad URIs intervals and prefixes", "[mqtt]")
{
    TEST_ESP_OK(mqtt_svc_config_validate_uri("mqtt://broker.local"));
    TEST_ESP_OK(mqtt_svc_config_validate_uri("mqtt://192.168.1.10:1883"));
    TEST_ESP_OK(mqtt_svc_config_validate_uri("mqtts://test.mosquitto.org:8883"));

    static const char *const bad_uris[] = {
        "",
        "broker.local",
        "http://broker.local",
        "MQTT://broker.local",
        "ws://broker.local",
        "mqtt://",
        "mqtts://",
        "mqtt://:1883",
        "mqtt:///path",
        "mqtt://user:secret@broker.local",
        "mqtt://bro ker.local",
        "mqtt://broker\".local",
    };
    for (size_t index = 0U; index < (sizeof(bad_uris) / sizeof(bad_uris[0])); ++index) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, mqtt_svc_config_validate_uri(bad_uris[index]), bad_uris[index]);
    }

    char long_uri[MQTT_SVC_URI_MAX_LENGTH + 2];
    memset(long_uri, 'a', sizeof(long_uri) - 1U);
    memcpy(long_uri, "mqtt://", 7);
    long_uri[sizeof(long_uri) - 1U] = '\0';
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, mqtt_svc_config_validate_uri(long_uri));
    long_uri[MQTT_SVC_URI_MAX_LENGTH] = '\0';
    TEST_ESP_OK(mqtt_svc_config_validate_uri(long_uri));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mqtt_svc_config_validate_interval(0U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mqtt_svc_config_validate_interval(199U));
    TEST_ESP_OK(mqtt_svc_config_validate_interval(200U));
    TEST_ESP_OK(mqtt_svc_config_validate_interval(MQTT_SVC_MAX_INTERVAL_MS));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mqtt_svc_config_validate_interval(MQTT_SVC_MAX_INTERVAL_MS + 1U));

    TEST_ESP_OK(mqtt_svc_config_validate_prefix("homeassistant"));
    TEST_ESP_OK(mqtt_svc_config_validate_prefix("lab/ha-2_test"));
    static const char *const bad_prefixes[] = {
        "", "/ha", "ha/", "ha//x", "ha/#", "ha/+", "home assistant", "ha$",
        "a23456789012345678901234567890123",
    };
    for (size_t index = 0U; index < (sizeof(bad_prefixes) / sizeof(bad_prefixes[0])); ++index) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, mqtt_svc_config_validate_prefix(bad_prefixes[index]), bad_prefixes[index]);
    }

    TEST_ESP_OK(mqtt_svc_config_validate_credential("", MQTT_SVC_USER_MAX_LENGTH));
    TEST_ESP_OK(mqtt_svc_config_validate_credential("pass with spaces", MQTT_SVC_PASSWORD_MAX_LENGTH));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mqtt_svc_config_validate_credential("tab\there", MQTT_SVC_PASSWORD_MAX_LENGTH));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, mqtt_svc_config_validate_credential("abcdef", 5U));

    mqtt_svc_config_t config;
    mqtt_svc_config_set_defaults(&config);
    TEST_ASSERT_FALSE(config.enabled);
    TEST_ASSERT_EQUAL_UINT32(1000U, config.interval_ms);
    TEST_ASSERT_EQUAL_STRING("homeassistant", config.prefix);
    TEST_ASSERT_EQUAL_STRING("", config.uri);
}

TEST_CASE("mqtt topics derive from the STA MAC and discovery prefix", "[mqtt]")
{
    mqtt_svc_topics_t topics;
    build_test_topics(&topics);

    TEST_ASSERT_EQUAL_STRING("a1b2c3", topics.device_id);
    TEST_ASSERT_EQUAL_STRING("psu_ext_a1b2c3", topics.client_id);
    TEST_ASSERT_EQUAL_STRING("psu_ext/a1b2c3/availability", topics.availability);
    TEST_ASSERT_EQUAL_STRING("psu_ext/a1b2c3/measurements", topics.measurements);
    TEST_ASSERT_EQUAL_STRING("psu_ext/a1b2c3/relay/state", topics.relay_state);
    TEST_ASSERT_EQUAL_STRING("psu_ext/a1b2c3/relay/set", topics.relay_set);
    TEST_ASSERT_EQUAL_STRING("psu_ext/a1b2c3/protection/state", topics.protection_state);
    TEST_ASSERT_EQUAL_STRING("homeassistant/device/psu_ext_a1b2c3/config", topics.discovery);
    TEST_ASSERT_EQUAL_STRING("homeassistant/status", topics.ha_status);
}

TEST_CASE("mqtt discovery payload declares all entities for one device", "[mqtt]")
{
    mqtt_svc_topics_t topics;
    build_test_topics(&topics);

    char *payload = mqtt_svc_payload_discovery(&topics, "1.2.3");
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_EQUAL_UINT(13U, count_occurrences(payload, "\"platform\":"));
    TEST_ASSERT_EQUAL_UINT(9U, count_occurrences(payload, "\"platform\":\"sensor\""));
    TEST_ASSERT_EQUAL_UINT(3U, count_occurrences(payload, "\"platform\":\"binary_sensor\""));
    TEST_ASSERT_EQUAL_UINT(1U, count_occurrences(payload, "\"platform\":\"switch\""));
    TEST_ASSERT_EQUAL_UINT(3U, count_occurrences(payload, "\"entity_category\":\"diagnostic\""));
    TEST_ASSERT_EQUAL_UINT(3U, count_occurrences(payload, "\"device_class\":\"problem\""));
    TEST_ASSERT_EQUAL_UINT(3U, count_occurrences(payload, "\"state_class\":\"measurement\""));

    TEST_ASSERT_NOT_NULL(strstr(payload, "\"identifiers\":[\"psu_ext_a1b2c3\"]"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"sw_version\":\"1.2.3\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"origin\":{"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"availability_topic\":\"psu_ext/a1b2c3/availability\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"command_topic\":\"psu_ext/a1b2c3/relay/set\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"state_topic\":\"psu_ext/a1b2c3/relay/state\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"optimistic\":false"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"unique_id\":\"psu_ext_a1b2c3_current_max\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"value_template\":\"{{ value_json.ocp }}\""));
    TEST_ASSERT_NULL(strstr(payload, "configuration_url"));

    mqtt_svc_payload_free(payload);
}
