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

static void publish_zero_calibration_samples(void *context)
{
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .physical_input = MEASURE_INPUT_ADS1115_AIN0,
        .pga_full_scale_mv = 256U,
    };
    /* Two discarded conversions followed by one accepted conversion. Delay
     * first so the caller can enter the blocking capture. */
    for (unsigned i = 0; i < 3; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10U));
        measure_svc_event_bus_publish(&event);
    }
    xSemaphoreGive((SemaphoreHandle_t)context);
    vTaskDelete(NULL);
}

static void stage_zero_calibration_point(void)
{
    uint32_t previous_average;
    TEST_ESP_OK(measure_svc_get_average_count(MEASURE_KIND_VOLTAGE, &previous_average));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_VOLTAGE, 1U));
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(done);
    TEST_ASSERT_EQUAL(pdPASS,
        xTaskCreate(publish_zero_calibration_samples, "scpi_cal_sample", 4096U, done, 4U, NULL));
    const esp_err_t captured = measure_svc_calibration_capture_point(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, 0U, NULL);
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_VOLTAGE, previous_average));
    TEST_ESP_OK(captured);
}

TEST_CASE("calibration scpi reports transaction states and malformed commands", "[calibration][scpi]")
{
    ensure_runtime();
    measure_svc_cal_transaction_t initial_transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&initial_transaction));
    if (initial_transaction.state == MEASURE_SVC_CAL_TRANSACTION_OPEN) {
        TEST_ESP_OK(measure_svc_calibration_abort());
    }

    char command[] = "CALibration:TRANsaction?";
    run_scpi(command);
    TEST_ASSERT_TRUE(s_saw_response);
    TEST_ASSERT_EQUAL_STRING("IDLE", s_last_response);

    /* Query our own staged fixture, independent of active/NVS calibration. */
    stage_zero_calibration_point();
    char point_query[] = "CALibration:VOLTage? CH0,POINt1";
    run_scpi(point_query);
    TEST_ASSERT_EQUAL_STRING("0.000000,0.0000,0.256", s_last_response);

    char valid_max_index[] = "CALibration:VOLTage? CH0,POINt32";
    run_scpi(valid_max_index);
    TEST_ASSERT_TRUE(strncmp(s_last_response, "ERR,", 4) == 0);
    TEST_ASSERT_NULL(strstr(s_last_response, "Expected"));

    char invalid_index[] = "CALibration:VOLTage? CH0,POINt33";
    run_scpi(invalid_index);
    TEST_ASSERT_NOT_NULL(strstr(s_last_response, "Expected"));

    TEST_ESP_OK(measure_svc_calibration_abort());
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

typedef struct {
    SemaphoreHandle_t started;
    SemaphoreHandle_t done;
    esp_err_t result;
} rel_start_task_t;

static void start_calibration_while_guarded(void *context)
{
    rel_start_task_t *args = context;
    xSemaphoreGive(args->started);
    args->result = measure_svc_calibration_start(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1);
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

TEST_CASE("calibration start waits for in-flight protection and clears REL", "[rel][calibration][concurrency]")
{
    ensure_runtime();
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 2U));
    rel_start_task_t args = {
        .started = xSemaphoreCreateBinary(),
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL,
    };
    TEST_ASSERT_NOT_NULL(args.started);
    TEST_ASSERT_NOT_NULL(args.done);
    bool bypassed;
    TEST_ESP_OK(measure_svc_calibration_protection_begin(&bypassed));
    BaseType_t created = xTaskCreate(start_calibration_while_guarded, "rel_start", 4096,
        &args, 5, NULL);
    BaseType_t started = pdFALSE;
    BaseType_t finished_early = pdFALSE;
    if (created == pdPASS) {
        started = xSemaphoreTake(args.started, pdMS_TO_TICKS(1000));
        finished_early = xSemaphoreTake(args.done, pdMS_TO_TICKS(20));
    }
    measure_svc_calibration_protection_end();
    TEST_ASSERT_EQUAL(pdPASS, created);
    BaseType_t finished = finished_early == pdTRUE ? pdTRUE :
        xSemaphoreTake(args.done, pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(pdTRUE, finished);
    vSemaphoreDelete(args.started);
    vSemaphoreDelete(args.done);
    TEST_ASSERT_EQUAL(pdTRUE, started);
    TEST_ASSERT_EQUAL(pdFALSE, finished_early);
    TEST_ASSERT_FALSE(bypassed);
    TEST_ESP_OK(args.result);
    uint32_t offset;
    TEST_ESP_OK(measure_svc_get_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, &offset));
    TEST_ASSERT_EQUAL_UINT32(0U, offset);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 2U));
    TEST_ESP_OK(measure_svc_calibration_abort());
}
