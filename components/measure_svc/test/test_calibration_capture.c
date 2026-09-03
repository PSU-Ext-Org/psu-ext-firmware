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
#include "../measure_svc_calibration_capture.h"
#include "../measure_svc_calibration_capture_config.h"
#include "../measure_svc_event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "unity.h"

typedef struct {
    esp_err_t result;
} capture_args_t;

static void ensure_runtime_and_idle(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if ((nvs_err == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        TEST_ESP_OK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    TEST_ESP_OK(nvs_err);

    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    TEST_ESP_OK(measure_svc_init_with_config(&config));
    measure_svc_cal_transaction_t transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    if (transaction.state != MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        TEST_ESP_OK(measure_svc_calibration_abort());
    }
}

static void capture_task(void *context)
{
    capture_args_t *args = context;
    args->result = measure_svc_calibration_capture_point(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, 10000U, NULL);
    vTaskDelete(NULL);
}

static void publish_code(int16_t code)
{
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .physical_input = MEASURE_INPUT_ADS1115_AIN0,
        .raw_code = code,
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
        .source_generation = source_generation,
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
    measure_svc_cal_capture_window_reset(&window);
    TEST_ASSERT_FALSE(measure_svc_cal_capture_window_add(&window, 2000));
    TEST_ASSERT_FALSE(measure_svc_cal_capture_window_add(&window, 3000));
    for (int16_t code = 1600; code < 1616; ++code) {
        const bool complete = measure_svc_cal_capture_window_add(&window, code);
        TEST_ASSERT_EQUAL(code == 1615, complete);
    }
    TEST_ASSERT_TRUE(measure_svc_cal_capture_window_is_stable(&window));
    TEST_ASSERT_EQUAL_INT16(1608, measure_svc_cal_capture_window_mean(&window));

    /* Rounding once after code-domain averaging preserves a result that
     * averaging individually rounded u4 samples would change (9 versus 10). */
    measure_svc_cal_capture_window_reset(&window);
    (void)measure_svc_cal_capture_window_add(&window, 0);
    (void)measure_svc_cal_capture_window_add(&window, 0);
    for (uint8_t i = 0U; i < MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES; ++i) {
        (void)measure_svc_cal_capture_window_add(&window, i < 2U ? 1 : 17);
    }
    TEST_ASSERT_TRUE(measure_svc_cal_capture_window_is_stable(&window));
    TEST_ASSERT_EQUAL_INT16(15, measure_svc_cal_capture_window_mean(&window));
    TEST_ASSERT_EQUAL_UINT32(9U, (15U * 20480U + 16383U) / 32767U);
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
    TEST_ESP_OK(measure_svc_calibration_abort());
}

TEST_CASE("calibration retries an unstable ADS1115-code window", "[calibration][capture]")
{
    measure_svc_cal_capture_window_t boundary;
    measure_svc_cal_capture_window_reset(&boundary);
    (void)measure_svc_cal_capture_window_add(&boundary, 0);
    (void)measure_svc_cal_capture_window_add(&boundary, 0);
    for (uint8_t i = 0U; i < MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES; ++i) {
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
    for (uint8_t i = 0U; i < MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES; ++i) {
        publish_code((i & 1U) ? 1617 : 1600);
    }
    publish_code(0);
    publish_code(0);
    for (uint8_t i = 0U; i < MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES; ++i) {
        publish_code(1600);
    }
    wait_for_result(&args);
    TEST_ESP_OK(args.result);
    TEST_ESP_OK(measure_svc_calibration_abort());

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    capture_args_t timeout_args = {.result = ESP_ERR_INVALID_STATE};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "timeout_cap", 4096U, &timeout_args, 5U, NULL));
    for (uint16_t i = 0U; (i < 3050U) && (timeout_args.result == ESP_ERR_INVALID_STATE); ++i) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, timeout_args.result);
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
             MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES);
         ++generation) {
        for (uint8_t duplicate = 0U; duplicate < 10U; ++duplicate) {
            publish_code_generation(1600, generation);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(20U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, args.result);

    publish_code_generation(
        1600,
        MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES + MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES);
    wait_for_result(&args);
    TEST_ESP_OK(args.result);
    TEST_ESP_OK(measure_svc_calibration_abort());
}
