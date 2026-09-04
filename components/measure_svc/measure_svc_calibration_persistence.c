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

#include <limits.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "measure_provider.h"
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
    uint16_t default_slope_numerator;
    uint16_t default_slope_denominator;
} cal_storage_target_t;

static const char *TAG = "measure_cal_nvs";

static const cal_storage_target_t s_targets[] = {
    {
        .name = "voltage CH0",
        .blob_key = CAL_NVS_KEY_V_CH0,
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_0,
        .default_slope_numerator = 35U,
        .default_slope_denominator = 2U,
    },
    {
        .name = "voltage CH1",
        .blob_key = CAL_NVS_KEY_V_CH1,
        .kind = MEASURE_KIND_VOLTAGE,
        .channel = MEASURE_CHANNEL_1,
        .default_slope_numerator = 35U,
        .default_slope_denominator = 2U,
    },
    {
        .name = "current CH1",
        .blob_key = CAL_NVS_KEY_I_CH1,
        .kind = MEASURE_KIND_CURRENT,
        .channel = MEASURE_CHANNEL_1,
        .default_slope_numerator = 2U,
        .default_slope_denominator = 1U,
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
    *table = (measure_svc_cal_table_t){0};
    uint16_t pgas[4];
    const size_t configured_count = measure_prov_ads1115_get_configured_pgas(
        pgas, sizeof(pgas) / sizeof(pgas[0]));
    const size_t range_count = configured_count <= (sizeof(pgas) / sizeof(pgas[0])) ?
        configured_count : 0U;
    for (size_t range = 0U; range < range_count; ++range) {
        const uint64_t actual_numerator =
            (uint64_t)INT16_MAX * pgas[range] * 10U * target->default_slope_numerator;
        const uint32_t actual_denominator =
            32768U * target->default_slope_denominator;
        table->points[table->count++] = (measure_svc_cal_table_point_t){
            .raw_code = 0,
            .actual_u4 = 0U,
            .pga_full_scale_mv = pgas[range],
        };
        table->points[table->count++] = (measure_svc_cal_table_point_t){
            .raw_code = INT16_MAX,
            .actual_u4 = (uint32_t)((actual_numerator + (actual_denominator / 2U)) /
                actual_denominator),
            .pga_full_scale_mv = pgas[range],
        };
    }
    measure_svc_cal_table_invalidate_cache(table);
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

    nvs_close(handle);
    ESP_LOGW(TAG, "valid %s calibration not found; using default", target->name);
    set_default(target, table);
    return ESP_OK;
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
