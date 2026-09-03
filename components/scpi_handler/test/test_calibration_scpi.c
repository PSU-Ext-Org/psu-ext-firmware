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

#include "measure_svc.h"
#include "measure_svc_calibration.h"
#include "scpi_handler.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

static char s_last_response[96];
static bool s_saw_response;

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

static void run_scpi(char *command)
{
    const scpi_handler_response_writer_t writer = {
        .write_text = capture_response,
        .write_binary = ignore_binary,
    };
    s_saw_response = false;
    s_last_response[0] = '\0';
    scpi_handler_handle_command(command, &writer);
}

static void ensure_runtime(void)
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

    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    TEST_ESP_OK(measure_svc_init_with_config(&config));
    initialized = true;
}

TEST_CASE("calibration scpi reports transaction states and malformed commands", "[calibration][scpi]")
{
    ensure_runtime();

    char command[] = "CALibration:TRANsaction?";
    run_scpi(command);
    TEST_ASSERT_TRUE(s_saw_response);
    TEST_ASSERT_EQUAL_STRING("IDLE", s_last_response);

    char start[] = "CALibration:STARt VOLTage,CH0";
    run_scpi(start);
    TEST_ASSERT_FALSE(s_saw_response);

    char transaction[] = "CALibration:TRANsaction?";
    run_scpi(transaction);
    TEST_ASSERT_EQUAL_STRING("OPEN,VOLTAGE,CH0", s_last_response);

    char wrong_count[] = "CALibration:CURRent:COUNt? CH0";
    run_scpi(wrong_count);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);

    char malformed_start[] = "CALibration:STARt VOLTage";
    run_scpi(malformed_start);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);

    char abort[] = "CALibration:ABORt";
    run_scpi(abort);
    TEST_ASSERT_FALSE(s_saw_response);

    char commit_idle[] = "CALibration:COMMit";
    run_scpi(commit_idle);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);
}

TEST_CASE("measurement ADC rate SCPI validates persists and blocks during calibration", "[measurement][scpi]")
{
    ensure_runtime();
    measure_svc_cal_transaction_t transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    if (transaction.state == MEASURE_SVC_CAL_TRANSACTION_OPEN) {
        TEST_ESP_OK(measure_svc_calibration_abort());
    }

    static const uint16_t rates[] = {8U, 16U, 32U, 64U, 128U, 250U, 475U, 860U};
    for (size_t index = 0U; index < (sizeof(rates) / sizeof(rates[0])); ++index) {
        char set_command[32];
        snprintf(set_command, sizeof(set_command), "MEAS:ADC:RATE %u", (unsigned)rates[index]);
        run_scpi(set_command);
        TEST_ASSERT_FALSE(s_saw_response);

        char query[] = "MEAS:ADC:RATE?";
        char expected[8];
        snprintf(expected, sizeof(expected), "%u", (unsigned)rates[index]);
        run_scpi(query);
        TEST_ASSERT_EQUAL_STRING(expected, s_last_response);
    }

    char invalid[] = "MEAS:ADC:RATE 9";
    run_scpi(invalid);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);
    char unchanged[] = "MEAS:ADC:RATE?";
    run_scpi(unchanged);
    TEST_ASSERT_EQUAL_STRING("860", s_last_response);

    nvs_handle_t handle;
    uint16_t stored_rate = 0U;
    TEST_ESP_OK(nvs_open("meas_cfg", NVS_READONLY, &handle));
    TEST_ESP_OK(nvs_get_u16(handle, "ads_rate_sps", &stored_rate));
    nvs_close(handle);
    TEST_ASSERT_EQUAL_UINT16(860U, stored_rate);

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    char blocked[] = "MEAS:ADC:RATE 128";
    run_scpi(blocked);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);
    TEST_ESP_OK(measure_svc_calibration_abort());

    char restore[] = "MEAS:ADC:RATE 128";
    run_scpi(restore);
    TEST_ASSERT_FALSE(s_saw_response);
}
