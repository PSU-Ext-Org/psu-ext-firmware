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
 * @file measure_svc_calibration_persistence.c
 * @brief NVS loading, migration, and storage for calibration tables.
 */

#include "measure_svc_calibration_persistence.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "measure_svc_calibration_record.h"
#include "nvs.h"

#define CAL_NVS_NAMESPACE "meas_cfg"

/* Versioned blobs. NVS keys are limited to 15 characters. */
#define CAL_NVS_KEY_V_CH0 "cal_v_ch0"
#define CAL_NVS_KEY_V_CH1 "cal_v_ch1"
#define CAL_NVS_KEY_I_CH1 "cal_i_ch1"

typedef struct {
    const char *name;
    const char *blob_key;
    measure_kind_t kind;
    measure_channel_t channel;
    const char *legacy_keys[4];
    uint32_t default_raw_u4;
    uint32_t default_actual_u4;
} cal_storage_target_t;

static const char *TAG = "measure_cal_nvs";

static const cal_storage_target_t s_targets[] = {
    {
        .name = "voltage CH0",
        .blob_key = CAL_NVS_KEY_V_CH0,
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .legacy_keys = {"in_v_p1_raw", "in_v_p1_act", "in_v_p2_raw", "in_v_p2_act"},
        .default_raw_u4 = 10000U,
        .default_actual_u4 = 175000U,
    },
    {
        .name = "voltage CH1",
        .blob_key = CAL_NVS_KEY_V_CH1,
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_1,
        .legacy_keys = {"ch0_p1_raw", "ch0_p1_act", "ch0_p2_raw", "ch0_p2_act"},
        .default_raw_u4 = 10000U,
        .default_actual_u4 = 175000U,
    },
    {
        .name = "current CH1",
        .blob_key = CAL_NVS_KEY_I_CH1,
        .kind = MEASURE_KIND_CURRENT,
        .channel = MEASURE_CHANNEL_1,
        .legacy_keys = {"cur_p1_raw", "cur_p1_act", "cur_p2_raw", "cur_p2_act"},
        .default_raw_u4 = 5000U,
        .default_actual_u4 = 10000U,
    },
};

static const cal_storage_target_t *find_target(
    measure_kind_t kind,
    measure_channel_t channel)
{
    for (size_t index = 0U; index < (sizeof(s_targets) / sizeof(s_targets[0])); ++index) {
        if ((s_targets[index].kind == kind) && (s_targets[index].channel == channel)) {
            return &s_targets[index];
        }
    }
    return NULL;
}

static void set_default(const cal_storage_target_t *target, measure_svc_cal_table_t *table)
{
    const measure_svc_cal_table_t default_table = {
        .count = 2U,
        .points = {
            {.raw_u4 = 0U, .actual_u4 = 0U},
            {.raw_u4 = target->default_raw_u4, .actual_u4 = target->default_actual_u4},
        },
    };

    *table = default_table;
    measure_svc_cal_table_invalidate_cache(table);
}

static esp_err_t load_legacy(
    nvs_handle_t handle,
    const cal_storage_target_t *target,
    measure_svc_cal_table_t *table)
{
    measure_svc_cal_table_t candidate = {.count = 2U};
    uint32_t *values[] = {
        &candidate.points[0].raw_u4,
        &candidate.points[0].actual_u4,
        &candidate.points[1].raw_u4,
        &candidate.points[1].actual_u4,
    };

    esp_err_t err = ESP_OK;
    for (size_t index = 0U; (index < 4U) && (err == ESP_OK); ++index) {
        err = nvs_get_u32(handle, target->legacy_keys[index], values[index]);
    }
    if (err != ESP_OK) {
        return err;
    }

    if ((candidate.points[0].raw_u4 > candidate.points[1].raw_u4) &&
        (candidate.points[0].actual_u4 > candidate.points[1].actual_u4)) {
        const measure_svc_cal_table_point_t first = candidate.points[0];
        candidate.points[0] = candidate.points[1];
        candidate.points[1] = first;
    }
    if (!measure_svc_cal_table_validate(&candidate)) {
        return ESP_ERR_INVALID_ARG;
    }

    measure_svc_cal_table_invalidate_cache(&candidate);
    *table = candidate;
    return ESP_OK;
}

esp_err_t measure_svc_cal_persistence_load(
    measure_kind_t kind,
    measure_channel_t channel,
    measure_svc_cal_table_t *table)
{
    const cal_storage_target_t *target = find_target(kind, channel);
    if ((target == NULL) || (table == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    set_default(target, table);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CAL_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "%s calibration not found; using default", target->name);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE];
    size_t record_size = sizeof(record);
    err = nvs_get_blob(handle, target->blob_key, record, &record_size);
    if (err == ESP_OK) {
        nvs_close(handle);
        if ((record_size != sizeof(record)) || !measure_svc_cal_record_decode(record, table)) {
            ESP_LOGW(TAG, "invalid %s calibration blob; using default", target->name);
            set_default(target, table);
        }
        return ESP_OK;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        if ((err == ESP_ERR_NVS_INVALID_LENGTH) || (err == ESP_ERR_NVS_TYPE_MISMATCH)) {
            ESP_LOGW(TAG, "malformed %s calibration blob; using default", target->name);
            return ESP_OK;
        }
        return err;
    }

    measure_svc_cal_table_t legacy_table;
    err = load_legacy(handle, target, &legacy_table);
    nvs_close(handle);
    if (err == ESP_OK) {
        *table = legacy_table;
        ESP_LOGI(TAG, "loaded legacy two-point %s calibration", target->name);
        return ESP_OK;
    }
    if ((err == ESP_ERR_NVS_NOT_FOUND) ||
        (err == ESP_ERR_NVS_TYPE_MISMATCH) ||
        (err == ESP_ERR_INVALID_ARG)) {
        ESP_LOGW(TAG, "valid %s calibration not found; using default", target->name);
        set_default(target, table);
        return ESP_OK;
    }
    return err;
}

esp_err_t measure_svc_cal_persistence_store(
    measure_kind_t kind,
    measure_channel_t channel,
    const measure_svc_cal_table_t *table)
{
    const cal_storage_target_t *target = find_target(kind, channel);
    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE];
    if ((target == NULL) || !measure_svc_cal_record_encode(table, record)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(
        nvs_open(CAL_NVS_NAMESPACE, NVS_READWRITE, &handle),
        TAG,
        "nvs_open failed");
    esp_err_t err = nvs_set_blob(handle, target->blob_key, record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
