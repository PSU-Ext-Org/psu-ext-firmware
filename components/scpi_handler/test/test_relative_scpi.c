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

TEST_CASE("REL commands validate offsets and calibration clears all targets", "[rel][scpi][calibration]")
{
    ensure_runtime();
    const char *sets[] = {"MEAS:VOLT:REL 0, 0.0001;", "MEAS:VOLT:REL CH1, 0.00025",
                          "MEAS:CURR:REL CH1, 0.0002;"};
    const char *queries[] = {"MEAS:VOLT:REL? CH0;", "MEAS:VOLT:REL? 1", "MEAS:CURR:REL? CH1"};
    const char *expected[] = {"0.0001", "0.0003", "0.0002"};
    for (unsigned i = 0; i < 3; ++i) {
        char command[80];
        strlcpy(command, sets[i], sizeof(command));
        run_scpi(command);
        TEST_ASSERT_FALSE(s_saw_response);
        strlcpy(command, queries[i], sizeof(command));
        run_scpi(command);
        TEST_ASSERT_EQUAL_STRING(expected[i], s_last_response);
    }
    const char *invalid[] = {"MEAS:CURR:REL CH0, 1", "MEAS:CURR:REL CH1, -1",
        "MEAS:CURR:REL CH1, nan", "MEAS:CURR:REL CH1, inf", "MEAS:CURR:REL CH1, 1e20",
        "MEAS:CURR:REL CH1, 1,2", "MEAS:CURR:REL CH1,", "MEAS:CURR:REL CH1",
        "MEAS:CURR:REL? CH1,0", "MEAS:CURR:REL CH1, 2;;"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char command[80];
        strlcpy(command, invalid[i], sizeof(command));
        run_scpi(command);
        TEST_ASSERT_EQUAL_MEMORY("ERR,", s_last_response, 4);
    }
    uint32_t offset;
    TEST_ESP_OK(measure_svc_get_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, &offset));
    TEST_ASSERT_EQUAL_UINT32(2U, offset);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, measure_svc_calibration_start(MEASURE_KIND_POWER, MEASURE_CHANNEL_1));
    TEST_ESP_OK(measure_svc_get_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, &offset));
    TEST_ASSERT_EQUAL_UINT32(2U, offset);
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1));
    for (unsigned i = 0; i < 3; ++i) {
        char command[80];
        strlcpy(command, queries[i], sizeof(command));
        run_scpi(command);
        TEST_ASSERT_EQUAL_STRING("0.0000", s_last_response);
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 1U));
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0U));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, measure_svc_calibration_commit());
    bool bypassed;
    TEST_ESP_OK(measure_svc_calibration_protection_begin(&bypassed));
    measure_svc_calibration_protection_end();
    TEST_ASSERT_TRUE(bypassed);
    TEST_ESP_OK(measure_svc_calibration_abort());
    TEST_ESP_OK(measure_svc_calibration_protection_begin(&bypassed));
    measure_svc_calibration_protection_end();
    TEST_ASSERT_FALSE(bypassed);
}

TEST_CASE("REL subtracts after averaging without altering history", "[rel][scpi]")
{
    ensure_runtime();
    uint32_t previous_count;
    TEST_ESP_OK(measure_svc_get_average_count(MEASURE_KIND_CURRENT, &previous_count));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_CURRENT, 2U));
    for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY; ++i) {
        const measure_svc_sample_event_t event = {
            .kind = MEASURE_KIND_CURRENT, .channel = MEASURE_CHANNEL_1,
            .precise_u4 = (i % 2U) == 0 ? 2.0 : 4.0,
            .value_u4 = (i % 2U) == 0 ? 2U : 4U,
        };
        measure_svc_event_bus_publish(&event);
    }
    const uint32_t offsets[] = {1U, 3U, 4U, 0U};
    const uint32_t expected[] = {2U, 0U, 0U, 3U};
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t value;
        TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, offsets[i]));
        TEST_ESP_OK(measure_svc_read_relative(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, &value));
        TEST_ASSERT_EQUAL_UINT32(expected[i], value);
        char query[] = "MEAS:CURR? CH1";
        char response[24];
        snprintf(response, sizeof(response), "0.%04lu", (unsigned long)expected[i]);
        run_scpi(query);
        TEST_ASSERT_EQUAL_STRING(response, s_last_response);
        TEST_ESP_OK(measure_svc_read(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, &value));
        TEST_ASSERT_EQUAL_UINT32(3U, value);
    }
    measure_svc_sample_t samples[2];
    size_t copied;
    TEST_ESP_OK(measure_svc_copy_current_samples(MEASURE_CHANNEL_1,
        MEASURE_SVC_MAX_SAMPLE_CAPACITY - 2U, 2, samples, &copied));
    TEST_ASSERT_EQUAL_UINT32(2U, copied);
    TEST_ASSERT_EQUAL_UINT32(2U, samples[0].value_u4);
    TEST_ASSERT_EQUAL_UINT32(4U, samples[1].value_u4);
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_CURRENT, previous_count));
}

