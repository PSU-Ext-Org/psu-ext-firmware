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

#include "esp_event.h"
#include "measure_svc.h"
#include "mqtt_svc.h"
#include "output_ctrl.h"
#include "scpi_handler.h"
#include "wifi_manager.h"

#include "nvs_flash.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define TEST_MQTT_SECRET "s3cr3t-Pa55"

static char s_last_response[192];
static char s_all_responses[1024];
static bool s_saw_response;

static void capture_response(const char *response)
{
    s_saw_response = true;
    strlcpy(s_last_response, response, sizeof(s_last_response));
    strlcat(s_all_responses, response, sizeof(s_all_responses));
    strlcat(s_all_responses, "\n", sizeof(s_all_responses));
}

static void ignore_binary(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
}

static void run_scpi(const char *command)
{
    char buffer[128];
    const scpi_handler_response_writer_t writer = {
        .write_text = capture_response,
        .write_binary = ignore_binary,
    };
    strlcpy(buffer, command, sizeof(buffer));
    s_saw_response = false;
    s_last_response[0] = '\0';
    scpi_handler_handle_command(buffer, &writer);
}

static void assert_scpi_ok(const char *command)
{
    run_scpi(command);
    TEST_ASSERT_FALSE_MESSAGE(s_saw_response, s_last_response);
}

static void assert_scpi_error(const char *command)
{
    run_scpi(command);
    TEST_ASSERT_TRUE_MESSAGE(strncmp(s_last_response, "ERR,", 4) == 0, command);
}

static void assert_scpi_response(const char *command, const char *expected)
{
    run_scpi(command);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, s_last_response, command);
}

static void ensure_mqtt_runtime(void)
{
    static bool initialized;
    if (initialized) {
        return;
    }

    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        TEST_ESP_OK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    TEST_ESP_OK(err);

    err = esp_event_loop_create_default();
    TEST_ASSERT_TRUE((err == ESP_OK) || (err == ESP_ERR_INVALID_STATE));

    const measure_svc_config_t measure_config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    TEST_ESP_OK(wifi_manager_init());
    TEST_ESP_OK(output_ctrl_init());
    TEST_ESP_OK(measure_svc_init_with_config(&measure_config));
    TEST_ESP_OK(mqtt_svc_init());
    initialized = true;
}

TEST_CASE("mqtt scpi validates and persists broker settings", "[mqtt][scpi]")
{
    ensure_mqtt_runtime();
    assert_scpi_ok("SYST:MQTT:ENAB 0");

    assert_scpi_ok("SYST:MQTT:URI \"mqtt://broker.local:1883\"");
    assert_scpi_response("SYST:MQTT:URI?", "\"mqtt://broker.local:1883\"");
    assert_scpi_ok(":SYSTEM:MQTT:URI \"mqtts://test.mosquitto.org:8883\"");
    assert_scpi_response("syst:mqtt:uri?", "\"mqtts://test.mosquitto.org:8883\"");

    static const char *const bad_uris[] = {
        "SYST:MQTT:URI",
        "SYST:MQTT:URI \"\"",
        "SYST:MQTT:URI \"http://broker.local\"",
        "SYST:MQTT:URI \"mqtt://\"",
        "SYST:MQTT:URI \"mqtt://user:pw@broker.local\"",
        "SYST:MQTT:URI \"mqtt://bro ker\"",
        "SYST:MQTT:URI \"mqtt://broker.local",
        "SYST:MQTT:URI \"mqtt://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
    };
    for (size_t index = 0U; index < (sizeof(bad_uris) / sizeof(bad_uris[0])); ++index) {
        assert_scpi_error(bad_uris[index]);
    }
    assert_scpi_response("SYST:MQTT:URI?", "\"mqtts://test.mosquitto.org:8883\"");

    assert_scpi_error("SYST:MQTT:INT 199");
    assert_scpi_error("SYST:MQTT:INT 3600001");
    assert_scpi_error("SYST:MQTT:INT abc");
    assert_scpi_error("SYST:MQTT:INT -5");
    assert_scpi_error("SYST:MQTT:INT");
    assert_scpi_ok("SYST:MQTT:INT 200");
    assert_scpi_response("SYST:MQTT:INT?", "200");
    assert_scpi_ok("SYST:MQTT:INTERVAL 1000");
    assert_scpi_response("SYST:MQTT:INT?", "1000");

    assert_scpi_error("SYST:MQTT:PREF \"ha/#\"");
    assert_scpi_error("SYST:MQTT:PREF \"/ha\"");
    assert_scpi_error("SYST:MQTT:PREF \"\"");
    assert_scpi_ok("SYST:MQTT:PREF \"lab/ha\"");
    assert_scpi_response("SYST:MQTT:PREF?", "\"lab/ha\"");
    assert_scpi_ok("SYST:MQTT:PREF \"homeassistant\"");
    assert_scpi_response("SYST:MQTT:PREF?", "\"homeassistant\"");

    assert_scpi_ok("SYST:MQTT:USER \"psu\"");
    assert_scpi_response("SYST:MQTT:USER?", "\"psu\"");
    assert_scpi_ok("SYST:MQTT:USER \"\"");
    assert_scpi_response("SYST:MQTT:USER?", "\"\"");

    assert_scpi_error("SYST:MQTT:ENAB 2");
    assert_scpi_error("SYST:MQTT:ENAB");
    assert_scpi_response("SYST:MQTT:ENAB?", "0");

    assert_scpi_error("SYST:MQTT:BOGUS");
}

TEST_CASE("mqtt scpi password is write only and never echoed", "[mqtt][scpi]")
{
    ensure_mqtt_runtime();
    s_all_responses[0] = '\0';

    assert_scpi_ok("SYST:MQTT:PASS \"" TEST_MQTT_SECRET "\"");
    assert_scpi_response("SYST:MQTT:PASS?", "SET");

    run_scpi("SYST:MQTT:URI?");
    run_scpi("SYST:MQTT:USER?");
    run_scpi("SYST:MQTT:PREF?");
    run_scpi("SYST:MQTT:STAT?");
    run_scpi("SYST:MQTT:PASS \"" TEST_MQTT_SECRET "\x01\"");
    TEST_ASSERT_NULL(strstr(s_all_responses, TEST_MQTT_SECRET));
    TEST_ASSERT_NULL(strstr(s_all_responses, "s3cr3t"));

    assert_scpi_ok("SYST:MQTT:PASS \"\"");
    assert_scpi_response("SYST:MQTT:PASS?", "EMPTY");
}

TEST_CASE("mqtt scpi status reports state client id and error", "[mqtt][scpi]")
{
    ensure_mqtt_runtime();

    mqtt_svc_status_t status;
    TEST_ESP_OK(mqtt_svc_get_status(&status));
    TEST_ASSERT_EQUAL_UINT(14U, strlen(status.client_id));
    TEST_ASSERT_EQUAL_INT(0, strncmp(status.client_id, "psu_ext_", 8));

    char expected[64];

    assert_scpi_ok("SYST:MQTT:ENAB 0");
    snprintf(expected, sizeof(expected), "DISABLED,\"%s\",\"\"", status.client_id);
    assert_scpi_response("SYST:MQTT:STAT?", expected);

    /* The test app has no Wi-Fi connection, so an enabled client waits. */
    assert_scpi_ok("SYST:MQTT:URI \"mqtt://broker.local\"");
    assert_scpi_ok("SYST:MQTT:ENAB 1");
    assert_scpi_response("SYST:MQTT:ENAB?", "1");
    snprintf(expected, sizeof(expected), "WAIT_WIFI,\"%s\",\"\"", status.client_id);
    assert_scpi_response("SYSTEM:MQTT:STATUS?", expected);

    assert_scpi_ok("SYST:MQTT:ENAB 0");
    assert_scpi_response("SYST:MQTT:ENAB?", "0");
}
