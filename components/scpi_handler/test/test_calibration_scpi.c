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
