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
#include "measure_svc_samples.h"
#include "../../measure_svc/measure_svc_event_bus.h"
#include "scpi_handler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "scpi_test_fixture.h"

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


TEST_CASE("measurement query extra arguments retain legacy behavior", "[measurement][scpi]")
{
    ensure_runtime();
    const char *queries[] = {"MEAS:ADC:RATE?", "MEAS:VOLT:AVER:COUN?",
        "MEAS:CURR:AVER:COUN?", "MEAS:POWER:AVER:COUN?"};
    for (unsigned i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        char command[80];
        char expected[96];
        strlcpy(command, queries[i], sizeof(command));
        run_scpi(command);
        strlcpy(expected, s_last_response, sizeof(expected));
        snprintf(command, sizeof(command), "%s ignored,arguments", queries[i]);
        run_scpi(command);
        TEST_ASSERT_EQUAL_STRING(expected, s_last_response);
    }
    char scalar[] = "MEAS:VOLT? CH1,extra";
    run_scpi(scalar);
    TEST_ASSERT_NOT_NULL(strstr(s_last_response, "Expected measurement channel"));
    char transaction[] = "CAL:TRAN? extra";
    run_scpi(transaction);
    TEST_ASSERT_NOT_NULL(strstr(s_last_response, "with no arguments"));
}
