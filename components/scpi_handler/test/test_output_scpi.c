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

/*
 * Regression tests for OUTP. These drive the real CH1 relay; run them with the
 * output load disconnected.
 */

#include "measure_svc.h"
#include "output_ctrl.h"
#include "protection_svc.h"
#include "scpi_handler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#define TEST_OUTPUT_HARD_CURRENT_LIMIT_U4 25000U
#define TEST_OUTPUT_MAX_OVP_LIMIT_U4 300000U

static char s_last_response[96];
static bool s_saw_response;
static output_ctrl_change_event_t s_last_event;
static unsigned s_event_count;

static void capture_response(const char *response)
{
    s_saw_response = true;
    strlcpy(s_last_response, response, sizeof(s_last_response));
}

static void ignore_binary(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
}

static void capture_output_event(const output_ctrl_change_event_t *event, void *context)
{
    (void)context;
    s_last_event = *event;
    s_event_count++;
}

static void run_scpi(const char *command)
{
    char buffer[64];
    const scpi_handler_response_writer_t writer = {
        .write_text = capture_response,
        .write_binary = ignore_binary,
    };
    strlcpy(buffer, command, sizeof(buffer));
    s_saw_response = false;
    s_last_response[0] = '\0';
    scpi_handler_handle_command(buffer, &writer);
}

static void assert_output_query(const char *expected)
{
    run_scpi("OUTP? CH1");
    TEST_ASSERT_TRUE(s_saw_response);
    TEST_ASSERT_EQUAL_STRING(expected, s_last_response);
}

static void ensure_output_runtime(void)
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

    const measure_svc_config_t measure_config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    const protection_svc_limits_t limits = {
        .hard_current_limit_u4 = TEST_OUTPUT_HARD_CURRENT_LIMIT_U4,
        .max_ovp_limit_u4 = TEST_OUTPUT_MAX_OVP_LIMIT_U4,
    };

    TEST_ESP_OK(output_ctrl_init());
    TEST_ESP_OK(measure_svc_init_with_config(&measure_config));
    TEST_ESP_OK(protection_svc_init_with_limits(&limits));
    TEST_ESP_OK(output_ctrl_register_listener(capture_output_event, NULL));
    initialized = true;
}

static void reset_output_off(void)
{
    TEST_ESP_OK(output_ctrl_set_with_cause(PROTECTION_SVC_CHANNEL_CH1, false, OUTPUT_CTRL_CHANGE_CAUSE_SCPI));
    s_event_count = 0U;
    memset(&s_last_event, 0, sizeof(s_last_event));
}

TEST_CASE("output scpi rejects malformed commands and CH0", "[output][scpi]")
{
    ensure_output_runtime();
    reset_output_off();

    static const char *const malformed[] = {
        "OUTP",
        "OUTP CH1",
        "OUTP CH1,",
        "OUTP CH1,2",
        "OUTP CH1,ON",
        "OUTP CH9,1",
    };
    for (size_t index = 0U; index < (sizeof(malformed) / sizeof(malformed[0])); ++index) {
        run_scpi(malformed[index]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR,\"Expected OUTP <channel>,<0|1>\"", s_last_response, malformed[index]);
    }

    run_scpi("OUTP CH0,1");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Expected channel 1 or CH1\"", s_last_response);
    run_scpi("OUTP 0,1");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Expected channel 1 or CH1\"", s_last_response);

    run_scpi("OUTP? CH9");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Expected channel 1 or CH1\"", s_last_response);

    TEST_ASSERT_EQUAL_UINT(0U, s_event_count);
    assert_output_query("0");
}

TEST_CASE("output scpi switches CH1 and reports SCPI cause", "[output][scpi]")
{
    ensure_output_runtime();
    reset_output_off();

    uint32_t original_ch0_ovp_u4;
    TEST_ESP_OK(protection_svc_get_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, &original_ch0_ovp_u4));
    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, TEST_OUTPUT_MAX_OVP_LIMIT_U4));

    run_scpi("OUTP CH1,1");
    TEST_ASSERT_FALSE_MESSAGE(s_saw_response, s_last_response);
    assert_output_query("1");
    run_scpi("OUTP? 1");
    TEST_ASSERT_EQUAL_STRING("1", s_last_response);
    TEST_ASSERT_EQUAL_UINT(1U, s_event_count);
    TEST_ASSERT_TRUE(s_last_event.enabled);
    TEST_ASSERT_EQUAL_UINT8(PROTECTION_SVC_CHANNEL_CH1, s_last_event.channel);
    TEST_ASSERT_EQUAL(OUTPUT_CTRL_CHANGE_CAUSE_SCPI, s_last_event.cause);

    run_scpi("outp 1, 0");
    TEST_ASSERT_FALSE_MESSAGE(s_saw_response, s_last_response);
    assert_output_query("0");
    TEST_ASSERT_EQUAL_UINT(2U, s_event_count);
    TEST_ASSERT_FALSE(s_last_event.enabled);
    TEST_ASSERT_EQUAL(OUTPUT_CTRL_CHANGE_CAUSE_SCPI, s_last_event.cause);

    /* OFF while already off still applies and notifies. */
    run_scpi("OUTP CH1,0");
    TEST_ASSERT_FALSE(s_saw_response);
    TEST_ASSERT_EQUAL_UINT(3U, s_event_count);

    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, original_ch0_ovp_u4));
}

TEST_CASE("output scpi enable is blocked by CH0 input OVP", "[output][scpi]")
{
    ensure_output_runtime();
    reset_output_off();

    uint32_t input_voltage_u4 = 0U;
    const esp_err_t read_err = measure_svc_read(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, &input_voltage_u4);
    if ((read_err != ESP_OK) || (input_voltage_u4 == 0U)) {
        TEST_IGNORE_MESSAGE("needs a non-zero CH0 input voltage reading");
    }

    uint32_t original_ch0_ovp_u4;
    TEST_ESP_OK(protection_svc_get_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, &original_ch0_ovp_u4));
    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, 0U));

    run_scpi("OUTP CH1,1");
    TEST_ASSERT_EQUAL_STRING("ERR,\"OUTP failed: ESP_ERR_INVALID_STATE\"", s_last_response);
    assert_output_query("0");
    TEST_ASSERT_EQUAL_UINT(0U, s_event_count);

    bool ch0_tripped = false;
    TEST_ESP_OK(protection_svc_get_ovp_tripped(PROTECTION_SVC_CHANNEL_CH0, &ch0_tripped));
    TEST_ASSERT_TRUE(ch0_tripped);

    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, original_ch0_ovp_u4));
    TEST_ESP_OK(protection_svc_clear_trips(PROTECTION_SVC_CHANNEL_CH0));
}

TEST_CASE("output scpi enable clears a latched CH1 OCP trip", "[output][scpi]")
{
    ensure_output_runtime();
    reset_output_off();

    uint32_t original_ch0_ovp_u4;
    uint32_t original_ocp_u4;
    bool original_ocp_enabled;
    TEST_ESP_OK(protection_svc_get_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, &original_ch0_ovp_u4));
    TEST_ESP_OK(protection_svc_get_ocp_u4(PROTECTION_SVC_CHANNEL_CH1, &original_ocp_u4));
    TEST_ESP_OK(protection_svc_get_ocp_enabled(PROTECTION_SVC_CHANNEL_CH1, &original_ocp_enabled));

    /* Latch OCP from any non-zero current reading. */
    TEST_ESP_OK(protection_svc_set_ocp_u4(PROTECTION_SVC_CHANNEL_CH1, 0U));
    TEST_ESP_OK(protection_svc_set_ocp_enabled(PROTECTION_SVC_CHANNEL_CH1, true));

    bool ocp_tripped = false;
    for (int attempt = 0; (attempt < 50) && !ocp_tripped; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ESP_OK(protection_svc_get_ocp_tripped(PROTECTION_SVC_CHANNEL_CH1, &ocp_tripped));
    }

    TEST_ESP_OK(protection_svc_set_ocp_u4(PROTECTION_SVC_CHANNEL_CH1, original_ocp_u4));
    TEST_ESP_OK(protection_svc_set_ocp_enabled(PROTECTION_SVC_CHANNEL_CH1, original_ocp_enabled));

    if (!ocp_tripped) {
        TEST_IGNORE_MESSAGE("needs a non-zero CH1 current reading to latch OCP");
    }

    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, TEST_OUTPUT_MAX_OVP_LIMIT_U4));
    s_event_count = 0U;

    run_scpi("OUTP CH1,1");
    TEST_ASSERT_FALSE_MESSAGE(s_saw_response, s_last_response);
    TEST_ESP_OK(protection_svc_get_ocp_tripped(PROTECTION_SVC_CHANNEL_CH1, &ocp_tripped));
    TEST_ASSERT_FALSE(ocp_tripped);
    TEST_ASSERT_TRUE(s_event_count >= 1U);

    reset_output_off();
    TEST_ESP_OK(protection_svc_set_ovp_u4(PROTECTION_SVC_CHANNEL_CH0, original_ch0_ovp_u4));
}
