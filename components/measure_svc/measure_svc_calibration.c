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

/**
 * @file measure_svc_calibration.c
 * @brief Active calibration tables and single-table transaction service.
 */

#include "measure_svc_calibration.h"
#include "measure_svc_calibration_capture.h"
#include "measure_svc_calibration_persistence.h"
#include "measure_svc_calibration_runtime.h"
#include "measure_svc_calibration_table.h"
#include "measure_svc_core.h"

#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    measure_kind_t kind;
    measure_channel_t channel;
    measure_svc_cal_table_t *active_table;
} cal_target_t;

static const char *TAG = "measure_cal";

static SemaphoreHandle_t s_calibration_lock;
static bool s_calibration_initialized;
static measure_svc_cal_table_t s_voltage_ch0_table;
static measure_svc_cal_table_t s_voltage_ch1_table;
static measure_svc_cal_table_t s_current_ch1_table;

static measure_svc_cal_transaction_state_t s_transaction_state;
static const cal_target_t *s_transaction_target;
static measure_svc_cal_table_t s_staging_table;
static uint32_t s_transaction_generation;

static const cal_target_t s_targets[] = {
    {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .active_table = &s_voltage_ch0_table,
    },
    {
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_1,
        .active_table = &s_voltage_ch1_table,
    },
    {
        .kind = MEASURE_KIND_CURRENT,
        .channel = MEASURE_CHANNEL_1,
        .active_table = &s_current_ch1_table,
    },
};

static const cal_target_t *find_target(measure_kind_t kind, measure_channel_t channel)
{
    for (size_t index = 0U; index < (sizeof(s_targets) / sizeof(s_targets[0])); ++index) {
        if ((s_targets[index].kind == kind) && (s_targets[index].channel == channel)) {
            return &s_targets[index];
        }
    }
    return NULL;
}

static esp_err_t take_calibration_lock(void)
{
    if (s_calibration_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_calibration_lock, portMAX_DELAY) == pdTRUE ?
        ESP_OK :
        ESP_ERR_TIMEOUT;
}

static bool transaction_matches(const cal_target_t *target)
{
    return (target != NULL) &&
        (s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_IDLE) &&
        (s_transaction_target == target);
}

static const measure_svc_cal_table_t *query_table(const cal_target_t *target)
{
    return transaction_matches(target) ? &s_staging_table : target->active_table;
}

esp_err_t measure_svc_calibration_start(measure_kind_t kind, measure_channel_t channel)
{
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if (s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_staging_table = *target->active_table;
    measure_svc_cal_table_invalidate_cache(&s_staging_table);
    s_transaction_target = target;
    s_transaction_state = MEASURE_SVC_CAL_TRANSACTION_OPEN;
    ++s_transaction_generation;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_get_transaction(
    measure_svc_cal_transaction_t *transaction)
{
    if (transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    transaction->state = s_transaction_state;
    if (s_transaction_target != NULL) {
        transaction->kind = s_transaction_target->kind;
        transaction->channel = s_transaction_target->channel;
    } else {
        transaction->kind = MEASURE_KIND_VOLTAGE;
        transaction->channel = MEASURE_CHANNEL_0;
    }
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_abort(void)
{
    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if (s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_OPEN) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_staging_table, 0, sizeof(s_staging_table));
    s_transaction_target = NULL;
    s_transaction_state = MEASURE_SVC_CAL_TRANSACTION_IDLE;
    ++s_transaction_generation;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_capture_point(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t point_index,
    uint32_t actual_u4,
    measure_svc_cal_point_t *stored_point)
{
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((point_index == 0U) || (point_index > MEASURE_SVC_CAL_MAX_POINTS)) {
        return ESP_ERR_INVALID_ARG;
    }

    measure_input_t physical_input;
    ESP_RETURN_ON_ERROR(
        measure_svc_core_get_physical_input(channel, kind, &physical_input),
        TAG,
        "resolving calibration input failed");

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if ((s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_OPEN) ||
        !transaction_matches(target)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (point_index > (uint8_t)(s_staging_table.count + 1U)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t generation = s_transaction_generation;
    xSemaphoreGive(s_calibration_lock);

    int16_t raw_code;
    ESP_RETURN_ON_ERROR(
        measure_svc_calibration_capture_wait(
            channel,
            kind,
            physical_input,
            &raw_code),
        TAG,
        "waiting for fresh calibration sample failed");

    uint32_t raw_u4;
    ESP_RETURN_ON_ERROR(
        measure_svc_core_raw_code_to_u4(raw_code, &raw_u4),
        TAG,
        "converting fresh calibration sample failed");

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if ((s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_OPEN) ||
        !transaction_matches(target) ||
        (s_transaction_generation != generation)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (point_index > (uint8_t)(s_staging_table.count + 1U)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_ARG;
    }

    if (point_index == (uint8_t)(s_staging_table.count + 1U)) {
        s_staging_table.count = point_index;
    }
    s_staging_table.points[point_index - 1U].raw_u4 = raw_u4;
    s_staging_table.points[point_index - 1U].actual_u4 = actual_u4;
    measure_svc_cal_table_invalidate_cache(&s_staging_table);

    if (stored_point != NULL) {
        stored_point->raw_voltage_u4 = raw_u4;
        stored_point->actual_voltage_u4 = actual_u4;
    }
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_get_point(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t point_index,
    measure_svc_cal_point_t *point)
{
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((point == NULL) || (point_index == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    const measure_svc_cal_table_t *table = query_table(target);
    if (point_index > table->count) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_ARG;
    }

    point->raw_voltage_u4 = table->points[point_index - 1U].raw_u4;
    point->actual_voltage_u4 = table->points[point_index - 1U].actual_u4;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_clear(measure_kind_t kind, measure_channel_t channel)
{
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if ((s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_OPEN) ||
        !transaction_matches(target)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_staging_table, 0, sizeof(s_staging_table));
    measure_svc_cal_table_invalidate_cache(&s_staging_table);
    ++s_transaction_generation;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_get_count(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t *count)
{
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    *count = query_table(target)->count;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

esp_err_t measure_svc_calibration_commit(void)
{
    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if ((s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_OPEN) ||
        (s_transaction_target == NULL)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!measure_svc_cal_table_validate(&s_staging_table)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_ARG;
    }

    measure_svc_cal_table_t candidate = s_staging_table;
    const cal_target_t *target = s_transaction_target;
    ++s_transaction_generation;
    const uint32_t generation = s_transaction_generation;
    s_transaction_state = MEASURE_SVC_CAL_TRANSACTION_COMMITTING;
    xSemaphoreGive(s_calibration_lock);

    const esp_err_t store_err = measure_svc_cal_persistence_store(
        target->kind,
        target->channel,
        &candidate);

    ESP_RETURN_ON_ERROR(take_calibration_lock(), TAG, "taking calibration lock failed");
    if ((s_transaction_state != MEASURE_SVC_CAL_TRANSACTION_COMMITTING) ||
        (s_transaction_target != target) ||
        (s_transaction_generation != generation)) {
        xSemaphoreGive(s_calibration_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (store_err != ESP_OK) {
        s_transaction_state = MEASURE_SVC_CAL_TRANSACTION_OPEN;
        xSemaphoreGive(s_calibration_lock);
        return store_err;
    }

    measure_svc_cal_table_invalidate_cache(&candidate);
    *target->active_table = candidate;
    memset(&s_staging_table, 0, sizeof(s_staging_table));
    s_transaction_target = NULL;
    s_transaction_state = MEASURE_SVC_CAL_TRANSACTION_IDLE;
    ++s_transaction_generation;
    xSemaphoreGive(s_calibration_lock);
    return ESP_OK;
}

uint32_t measure_svc_calibration_apply_target_u4(
    measure_kind_t kind,
    measure_channel_t channel,
    uint32_t raw_u4)
{
    uint32_t actual_u4 = raw_u4;
    const cal_target_t *target = find_target(kind, channel);
    if (target == NULL) {
        return actual_u4;
    }
    if (take_calibration_lock() != ESP_OK) {
        return actual_u4;
    }
    (void)measure_svc_cal_table_apply_u4(target->active_table, raw_u4, &actual_u4);
    xSemaphoreGive(s_calibration_lock);
    return actual_u4;
}

esp_err_t measure_svc_calibration_init(void)
{
    if (s_calibration_initialized) {
        return ESP_OK;
    }
    if (s_calibration_lock == NULL) {
        s_calibration_lock = xSemaphoreCreateMutex();
        if (s_calibration_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    for (size_t index = 0U; index < (sizeof(s_targets) / sizeof(s_targets[0])); ++index) {
        ESP_RETURN_ON_ERROR(
            measure_svc_cal_persistence_load(
                s_targets[index].kind,
                s_targets[index].channel,
                s_targets[index].active_table),
            TAG,
            "loading calibration target failed");
    }
    s_calibration_initialized = true;
    return ESP_OK;
}
