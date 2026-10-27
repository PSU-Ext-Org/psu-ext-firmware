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
#include "measure_svc_precision.h"
#include "../measure_svc_calibration_capture.h"
#include "../measure_svc_calibration_capture_config.h"
#include "../measure_svc_event_bus.h"

#include <stdatomic.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "test_nvs.h"
#include "unity.h"

#define TEST_CAPTURE_SAMPLES 16U

typedef struct {
    esp_err_t result;
    bool shorten_timeout;
    bool timeout_injected;
    SemaphoreHandle_t done;
} capture_args_t;

/* Restrict deadline injection to the no-samples capture task. All other waits,
 * including successful capture retries, retain the production behavior. */
static capture_args_t *s_timeout_args;
static _Atomic(TaskHandle_t) s_timeout_task;

BaseType_t __real_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks_to_wait);
BaseType_t __wrap_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks_to_wait)
{
    if (xTaskGetCurrentTaskHandle() == atomic_load(&s_timeout_task) &&
        ticks_to_wait == MEASURE_SVC_CAL_CAPTURE_TIMEOUT_TICKS) {
        s_timeout_args->timeout_injected = true;
        ticks_to_wait = pdMS_TO_TICKS(100U);
    }
    return __real_xQueueSemaphoreTake(queue, ticks_to_wait);
}

static void ensure_runtime_and_idle(void)
{
    esp_err_t nvs_err = test_nvs_init();
    TEST_ESP_OK(nvs_err);

    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    TEST_ESP_OK(measure_svc_init_with_config(&config));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_VOLTAGE, TEST_CAPTURE_SAMPLES));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_CURRENT, TEST_CAPTURE_SAMPLES));
    measure_svc_cal_transaction_t transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    if (transaction.state != MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        TEST_ESP_OK(measure_svc_calibration_abort());
    }
}

static void capture_task(void *context)
{
    capture_args_t *args = context;
    if (args->shorten_timeout) {
        s_timeout_args = args;
        atomic_store(&s_timeout_task, xTaskGetCurrentTaskHandle());
    }
    args->result = measure_svc_calibration_capture_point(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, 10000U, NULL);
    if (args->shorten_timeout) {
        atomic_store(&s_timeout_task, NULL);
        s_timeout_args = NULL;
        xSemaphoreGive(args->done);
    }
    vTaskDelete(NULL);
}

static void publish_code(int16_t code)
{
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .physical_input = MEASURE_INPUT_ADS1115_AIN0,
        .raw_code = code,
        .precise_u4 = code,
        .value_u4 = measure_svc_precision_to_u4(code),
        .pga_full_scale_mv = 2048U,
    };
    measure_svc_event_bus_publish(&event);
}

static void publish_code_with_pga(int16_t code, uint16_t pga_full_scale_mv)
{
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .physical_input = MEASURE_INPUT_ADS1115_AIN0,
        .raw_code = code,
        .precise_u4 = code,
        .value_u4 = measure_svc_precision_to_u4(code),
        .pga_full_scale_mv = pga_full_scale_mv,
    };
    measure_svc_event_bus_publish(&event);
}

static void publish_code_generation(int16_t code, uint32_t source_generation)
{
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .physical_input = MEASURE_INPUT_ADS1115_AIN0,
        .raw_code = code,
        .precise_u4 = code,
        .value_u4 = measure_svc_precision_to_u4(code),
        .source_generation = source_generation,
        .pga_full_scale_mv = 2048U,
    };
    measure_svc_event_bus_publish(&event);
}

static void wait_for_result(const capture_args_t *args)
{
    for (uint8_t i = 0U; (i < 50U) && (args->result == ESP_ERR_INVALID_STATE); ++i) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

TEST_CASE("calibration captures a stable averaged ADS1115-code window", "[calibration][capture]")
{
    measure_svc_cal_capture_window_t window;
    measure_svc_cal_capture_window_reset(&window, TEST_CAPTURE_SAMPLES);
    TEST_ASSERT_FALSE(measure_svc_cal_capture_window_add(&window, 2000));
    TEST_ASSERT_FALSE(measure_svc_cal_capture_window_add(&window, 3000));
    for (int16_t code = 1600; code < 1616; ++code) {
        const bool complete = measure_svc_cal_capture_window_add(&window, code);
        TEST_ASSERT_EQUAL(code == 1615, complete);
    }
    TEST_ASSERT_TRUE(measure_svc_cal_capture_window_is_stable(&window));
    TEST_ASSERT_EQUAL_INT32(1607 * 65536 + 32768, measure_svc_cal_capture_window_mean_q16(&window));

    /* Rounding once after code-domain averaging preserves a result that
     * averaging individually rounded u4 samples would change (9 versus 10). */
    measure_svc_cal_capture_window_reset(&window, TEST_CAPTURE_SAMPLES);
    (void)measure_svc_cal_capture_window_add(&window, 0);
    (void)measure_svc_cal_capture_window_add(&window, 0);
    for (uint8_t i = 0U; i < TEST_CAPTURE_SAMPLES; ++i) {
        (void)measure_svc_cal_capture_window_add(&window, i < 2U ? 1 : 17);
    }
    TEST_ASSERT_TRUE(measure_svc_cal_capture_window_is_stable(&window));
    TEST_ASSERT_EQUAL_INT32(15 * 65536, measure_svc_cal_capture_window_mean_q16(&window));
    TEST_ASSERT_EQUAL_UINT32(9U, (15U * 20480U + 16384U) / 32768U);
    TEST_ASSERT_EQUAL_UINT32(10U, (2U * 1U + 14U * 11U + 8U) / 16U);

    ensure_runtime_and_idle();
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    capture_args_t args = {.result = ESP_ERR_INVALID_STATE};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "avg_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));
    publish_code(2000);
    publish_code(3000);
    for (int16_t code = 1600; code < 1616; ++code) {
        publish_code(code);
    }
    wait_for_result(&args);
    TEST_ESP_OK(args.result);
    measure_svc_cal_point_t point;
    TEST_ESP_OK(measure_svc_calibration_get_point(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, &point));
    TEST_ASSERT_EQUAL_UINT32(1005U, point.raw_voltage_u4);
    TEST_ASSERT_EQUAL_INT16(1608, point.raw_code);
    TEST_ESP_OK(measure_svc_calibration_abort());
}

TEST_CASE("calibration retries an unstable ADS1115-code window", "[calibration][capture]")
{
    measure_svc_cal_capture_window_t boundary;
    measure_svc_cal_capture_window_reset(&boundary, TEST_CAPTURE_SAMPLES);
    (void)measure_svc_cal_capture_window_add(&boundary, 0);
    (void)measure_svc_cal_capture_window_add(&boundary, 0);
    for (uint8_t i = 0U; i < TEST_CAPTURE_SAMPLES; ++i) {
        (void)measure_svc_cal_capture_window_add(&boundary, (i & 1U) ? 1616 : 1600);
    }
    TEST_ASSERT_TRUE(measure_svc_cal_capture_window_is_stable(&boundary));
    boundary.code_max = 1617;
    TEST_ASSERT_FALSE(measure_svc_cal_capture_window_is_stable(&boundary));

    ensure_runtime_and_idle();
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    capture_args_t args = {.result = ESP_ERR_INVALID_STATE};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "retry_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));
    publish_code(0);
    publish_code(0);
    for (uint8_t i = 0U; i < TEST_CAPTURE_SAMPLES; ++i) {
        publish_code((i & 1U) ? 1617 : 1600);
    }
    publish_code(0);
    publish_code(0);
    for (uint8_t i = 0U; i < TEST_CAPTURE_SAMPLES; ++i) {
        publish_code(1600);
    }
    wait_for_result(&args);
    TEST_ESP_OK(args.result);
    TEST_ESP_OK(measure_svc_calibration_abort());

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    capture_args_t timeout_args = {
        .result = ESP_ERR_INVALID_STATE,
        .shorten_timeout = true,
        .done = xSemaphoreCreateBinary(),
    };
    TEST_ASSERT_NOT_NULL(timeout_args.done);
    printf("Injecting a 100 ms wait for the no-samples capture timeout test\n");
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "timeout_cap", 4096U, &timeout_args, 5U, NULL));
    const BaseType_t completed = xSemaphoreTake(timeout_args.done, pdMS_TO_TICKS(1000U));
    /* If the hook regresses, wait for the worker to finish before its stack
     * context goes out of scope, then report the bounded-wait failure. */
    if (completed != pdTRUE) {
        xSemaphoreTake(timeout_args.done, portMAX_DELAY);
    }
    vSemaphoreDelete(timeout_args.done);
    TEST_ASSERT_EQUAL(pdTRUE, completed);
    TEST_ASSERT_TRUE(timeout_args.timeout_injected);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, timeout_args.result);
    TEST_ESP_OK(measure_svc_calibration_abort());
}

TEST_CASE("calibration restarts capture when PGA changes", "[calibration][capture]")
{
    ensure_runtime_and_idle();
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    capture_args_t args = {.result = ESP_ERR_INVALID_STATE};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "pga_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));

    for (uint8_t index = 0U; index < 7U; ++index) {
        publish_code_with_pga(1600, 512U);
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, args.result);
    for (uint8_t index = 0U;
         index < (MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES +
             TEST_CAPTURE_SAMPLES);
         ++index) {
        publish_code_with_pga(1600, 2048U);
    }
    wait_for_result(&args);
    TEST_ESP_OK(args.result);

    measure_svc_cal_point_t point;
    TEST_ESP_OK(measure_svc_calibration_get_point(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, &point));
    TEST_ASSERT_EQUAL_UINT16(2048U, point.pga_full_scale_mv);
    TEST_ESP_OK(measure_svc_calibration_abort());
}

TEST_CASE("calibration ignores repeated publications of one ADC conversion", "[calibration][capture]")
{
    ensure_runtime_and_idle();
    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));

    capture_args_t args = {.result = ESP_ERR_INVALID_STATE};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "unique_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));

    for (uint32_t generation = 1U;
         generation < (MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES +
             TEST_CAPTURE_SAMPLES);
         ++generation) {
        for (uint8_t duplicate = 0U; duplicate < 10U; ++duplicate) {
            publish_code_generation(1600, generation);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(20U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, args.result);

    publish_code_generation(
        1600,
        MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES + TEST_CAPTURE_SAMPLES);
    wait_for_result(&args);
    TEST_ESP_OK(args.result);
    TEST_ESP_OK(measure_svc_calibration_abort());
}
