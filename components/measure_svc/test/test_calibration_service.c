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
#include "../measure_svc_calibration_runtime.h"
#include "../measure_svc_calibration_persistence.h"
#include "../measure_svc_calibration_record.h"
#include "../measure_svc_calibration_capture_config.h"
#include "../measure_svc_event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "unity.h"

#define CAL_NVS_NAMESPACE "meas_cfg"
#define CAL_NVS_KEY_V_CH0 "cal_v_ch0"
#define CAL_NVS_KEY_I_CH1 "cal_i_ch1"

typedef struct {
    measure_kind_t kind;
    measure_channel_t channel;
    uint8_t point_index;
    uint32_t actual_u4;
    esp_err_t result;
} capture_task_args_t;

static void reset_nvs(void)
{
    (void)nvs_flash_deinit();
    TEST_ESP_OK(nvs_flash_erase());
    TEST_ESP_OK(nvs_flash_init());
}

static void ensure_measure_runtime(void)
{
    static bool initialized;
    if (initialized) {
        return;
    }

    reset_nvs();
    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
    };
    TEST_ESP_OK(measure_svc_init_with_config(&config));
    initialized = true;
}

static void ensure_calibration_transaction_idle(void)
{
    measure_svc_cal_transaction_t transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    if (transaction.state != MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        TEST_ESP_OK(measure_svc_calibration_abort());
    }
}

static measure_input_t input_for(measure_kind_t kind, measure_channel_t channel)
{
    if ((kind == MEASURE_KIND_VOLTAGE) && (channel == MEASURE_CHANNEL_0)) {
        return MEASURE_INPUT_ADS1115_AIN0;
    }
    if ((kind == MEASURE_KIND_VOLTAGE) && (channel == MEASURE_CHANNEL_1)) {
        return MEASURE_INPUT_ADS1115_AIN1;
    }
    return MEASURE_INPUT_ADS1115_AIN2;
}

static void capture_task(void *context)
{
    capture_task_args_t *args = (capture_task_args_t *)context;
    args->result = measure_svc_calibration_capture_point(
        args->kind,
        args->channel,
        args->point_index,
        args->actual_u4,
        NULL);
    vTaskDelete(NULL);
}

static esp_err_t capture_point_with_raw(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t point_index,
    uint32_t raw_u4,
    uint32_t actual_u4)
{
    capture_task_args_t args = {
        .kind = kind,
        .channel = channel,
        .point_index = point_index,
        .actual_u4 = actual_u4,
        .result = ESP_ERR_INVALID_STATE,
    };

    TEST_ASSERT_EQUAL(
        pdPASS,
        xTaskCreate(capture_task, "cal_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));
    const uint16_t pga_full_scale_mv = point_index <= 2U ? 256U :
        (point_index <= 4U ? 512U : 2048U);
    const uint32_t full_scale_u4 = (uint32_t)pga_full_scale_mv * 10U;
    const int16_t raw_code = (int16_t)(((uint64_t)raw_u4 * 32768U +
        (full_scale_u4 / 2U)) / full_scale_u4);
    for (uint8_t i = 0U;
         i < (MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES + MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES);
         ++i) {
        const measure_svc_sample_event_t event = {
            .channel = channel,
            .kind = kind,
            .physical_input = input_for(kind, channel),
            .raw_value_u4 = raw_u4,
            .raw_code = raw_code,
            .pga_full_scale_mv = pga_full_scale_mv,
            .value_u4 = raw_u4,
        };
        measure_svc_event_bus_publish(&event);
    }

    for (uint8_t i = 0; i < 50U; ++i) {
        if (args.result != ESP_ERR_INVALID_STATE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    return args.result;
}

static void store_calibration_raw_code(
    measure_kind_t kind,
    measure_channel_t channel,
    int16_t raw_code)
{
    const uint32_t raw_u4 = raw_code <= 0 ? 0U :
        (uint32_t)(((uint64_t)(uint16_t)raw_code * 20480U + 16384U) / 32768U);
    const measure_svc_sample_event_t event = {
        .channel = channel,
        .kind = kind,
        .physical_input = input_for(kind, channel),
        .raw_value_u4 = raw_u4,
        .raw_code = raw_code,
        .pga_full_scale_mv = 2048U,
        .value_u4 = raw_u4,
    };
    measure_svc_event_bus_publish(&event);
}

static void write_blob(const char *key, const uint8_t *record, size_t record_size)
{
    nvs_handle_t handle;
    TEST_ESP_OK(nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &handle));
    TEST_ESP_OK(nvs_set_blob(handle, key, record, record_size));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
}

TEST_CASE("calibration lifecycle edits staging and commits one target", "[calibration][lifecycle]")
{
    ensure_measure_runtime();
    ensure_calibration_transaction_idle();
    const int16_t high_code_2200 = (int16_t)(((uint64_t)2200U * 32768U + 10240U) / 20480U);

    const uint32_t active_before = measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, high_code_2200, 2048U);
    const uint32_t voltage_ch1_before = measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, 10000U, 2048U);
    const uint32_t current_ch1_before = measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 5000U, 2048U);

    uint8_t count = 0U;
    TEST_ESP_OK(measure_svc_calibration_get_count(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &count));
    TEST_ASSERT_EQUAL_UINT8(6U, count);

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        measure_svc_calibration_capture_point(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 1U, 1000U, NULL));

    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0));
    TEST_ESP_OK(measure_svc_calibration_get_count(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &count));
    TEST_ASSERT_EQUAL_UINT8(0U, count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, measure_svc_calibration_capture_point(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 3U, 1U, NULL));

    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, 1000U, 10000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, 1100U, 11000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 2U, 2200U, 22000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 3U, 1100U, 11000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 4U, 2200U, 22000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 5U, 1100U, 11000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 6U, 2200U, 22000U));

    measure_svc_cal_point_t point;
    TEST_ESP_OK(measure_svc_calibration_get_point(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, 1U, &point));
    TEST_ASSERT_EQUAL_UINT32(1100U, point.raw_voltage_u4);
    TEST_ASSERT_EQUAL_UINT32(11000U, point.actual_voltage_u4);

    TEST_ASSERT_EQUAL_UINT32(
        active_before,
        measure_svc_calibration_apply_target_u4(
            MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, high_code_2200, 2048U));
    TEST_ESP_OK(measure_svc_calibration_commit());
    TEST_ASSERT_EQUAL_UINT32(22000U, measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, high_code_2200, 2048U));
    TEST_ASSERT_EQUAL_UINT32(
        voltage_ch1_before,
        measure_svc_calibration_apply_target_u4(
            MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, 10000U, 2048U));
    TEST_ASSERT_EQUAL_UINT32(
        current_ch1_before,
        measure_svc_calibration_apply_target_u4(
            MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 5000U, 2048U));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, measure_svc_calibration_commit());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, measure_svc_calibration_abort());
}

TEST_CASE("calibration invalid commit and persistence failure keep transaction open", "[calibration][failure]")
{
    ensure_measure_runtime();
    ensure_calibration_transaction_idle();
    reset_nvs();

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 1U, 100U, 1000U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, measure_svc_calibration_commit());

    measure_svc_cal_transaction_t transaction;
    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    TEST_ASSERT_EQUAL(MEASURE_SVC_CAL_TRANSACTION_OPEN, transaction.state);

    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 2U, 200U, 2000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 3U, 100U, 1000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 4U, 200U, 2000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 5U, 100U, 1000U));
    TEST_ESP_OK(capture_point_with_raw(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 6U, 200U, 2000U));
    const uint32_t active_before = measure_svc_calibration_apply_target_u4(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 200U, 2048U);
    TEST_ESP_OK(nvs_flash_deinit());
    TEST_ASSERT_NOT_EQUAL(ESP_OK, measure_svc_calibration_commit());
    TEST_ASSERT_EQUAL_UINT32(active_before, measure_svc_calibration_apply_target_u4(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, 200U, 2048U));
    TEST_ESP_OK(nvs_flash_init());

    TEST_ESP_OK(measure_svc_calibration_get_transaction(&transaction));
    TEST_ASSERT_EQUAL(MEASURE_SVC_CAL_TRANSACTION_OPEN, transaction.state);
    TEST_ESP_OK(measure_svc_calibration_abort());
}

TEST_CASE("calibration stale capture cannot publish after abort", "[calibration][concurrency]")
{
    ensure_measure_runtime();
    ensure_calibration_transaction_idle();
    reset_nvs();

    TEST_ESP_OK(measure_svc_calibration_start(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1));
    TEST_ESP_OK(measure_svc_calibration_clear(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1));

    capture_task_args_t args = {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_1,
        .point_index = 1U,
        .actual_u4 = 12345U,
        .result = ESP_OK,
    };
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(capture_task, "stale_cap", 4096U, &args, 5U, NULL));
    vTaskDelay(pdMS_TO_TICKS(50U));
    TEST_ESP_OK(measure_svc_calibration_abort());
    for (uint8_t i = 0U;
         i < (MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES + MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES);
         ++i) {
        store_calibration_raw_code(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, 1974);
    }
    vTaskDelay(pdMS_TO_TICKS(100U));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, args.result);
    TEST_ASSERT_NOT_EQUAL(12345U, measure_svc_calibration_apply_target_u4(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, 1234U, 2048U));
}

TEST_CASE("calibration startup loads range-aware blobs and independently defaults invalid data", "[calibration][startup]")
{
    ensure_measure_runtime();
    ensure_calibration_transaction_idle();
    reset_nvs();

    const measure_svc_cal_table_t valid = {
        .count = 6U,
        .points = {
            {.raw_code = 10, .actual_u4 = 100U, .pga_full_scale_mv = 256U},
            {.raw_code = 10, .actual_u4 = 100U, .pga_full_scale_mv = 512U},
            {.raw_code = 10, .actual_u4 = 100U, .pga_full_scale_mv = 2048U},
            {.raw_code = 20, .actual_u4 = 200U, .pga_full_scale_mv = 256U},
            {.raw_code = 20, .actual_u4 = 200U, .pga_full_scale_mv = 512U},
            {.raw_code = 20, .actual_u4 = 200U, .pga_full_scale_mv = 2048U},
        },
    };
    TEST_ESP_OK(measure_svc_cal_persistence_store(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &valid));
    measure_svc_cal_table_t loaded = {0};
    TEST_ESP_OK(measure_svc_cal_persistence_load(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &loaded));
    TEST_ASSERT_EQUAL_UINT8(6U, loaded.count);
    TEST_ASSERT_EQUAL_UINT32(200U, loaded.points[5].actual_u4);

    reset_nvs();
    nvs_handle_t handle;
    TEST_ESP_OK(nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &handle));
    TEST_ESP_OK(nvs_set_u32(handle, "ch0_p1_raw", 30U));
    TEST_ESP_OK(nvs_set_u32(handle, "ch0_p1_act", 300U));
    TEST_ESP_OK(nvs_set_u32(handle, "ch0_p2_raw", 40U));
    TEST_ESP_OK(nvs_set_u32(handle, "ch0_p2_act", 400U));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
    TEST_ESP_OK(measure_svc_cal_persistence_load(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, &loaded));
    TEST_ASSERT_EQUAL_UINT8(6U, loaded.count);
    TEST_ASSERT_EQUAL_UINT16(512U, loaded.points[2].pga_full_scale_mv);

    reset_nvs();
    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE];
    TEST_ASSERT_TRUE(measure_svc_cal_record_encode(&valid, record));
    record[4] = 0xFFU;
    write_blob(CAL_NVS_KEY_V_CH0, record, sizeof(record));
    TEST_ESP_OK(measure_svc_cal_persistence_load(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &loaded));
    TEST_ASSERT_EQUAL_UINT32(44799U, loaded.points[1].actual_u4);

    reset_nvs();
    TEST_ASSERT_TRUE(measure_svc_cal_record_encode(&valid, record));
    record[MEASURE_SVC_CAL_RECORD_SIZE - 1U] ^= 0xA5U;
    write_blob(CAL_NVS_KEY_I_CH1, record, sizeof(record));
    TEST_ESP_OK(measure_svc_cal_persistence_store(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &valid));
    TEST_ESP_OK(measure_svc_cal_persistence_load(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, &loaded));
    TEST_ASSERT_EQUAL_UINT32(200U, loaded.points[5].actual_u4);
    TEST_ESP_OK(measure_svc_cal_persistence_load(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, &loaded));
    TEST_ASSERT_EQUAL_UINT32(5120U, loaded.points[1].actual_u4);
}
