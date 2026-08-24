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

#include "measure_svc_average.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "nvs.h"
#include "measure_svc_core.h"
#include "measure_svc_history.h"
#include "measure_svc_samples.h"

#define NVS_NAMESPACE "meas_cfg"
#define NVS_VOLTAGE_KEY "volt_avg_n"
#define NVS_CURRENT_KEY "curr_avg_n"
#define NVS_POWER_KEY "power_avg_n"

static const char *TAG = "measure_average";
static uint32_t s_counts[3] = {
    MEASURE_SVC_DEFAULT_AVERAGE_COUNT,
    MEASURE_SVC_DEFAULT_AVERAGE_COUNT,
    MEASURE_SVC_DEFAULT_AVERAGE_COUNT,
};
static measure_svc_sample_t s_samples[MEASURE_SVC_MAX_AVERAGE_COUNT];

static esp_err_t kind_slot(measure_kind_t kind, uint32_t **count, const char **key)
{
    static const char *keys[3] = {NVS_VOLTAGE_KEY, NVS_CURRENT_KEY, NVS_POWER_KEY};
    if ((count == NULL) || (key == NULL) ||
        (kind < MEASURE_KIND_VOLTAGE) || (kind > MEASURE_KIND_POWER)) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = &s_counts[(size_t)kind];
    *key = keys[(size_t)kind];
    return ESP_OK;
}

static uint32_t clamp_count(uint32_t count)
{
    if (count < MEASURE_SVC_MIN_AVERAGE_COUNT) {
        return MEASURE_SVC_MIN_AVERAGE_COUNT;
    }
    return count > MEASURE_SVC_MAX_AVERAGE_COUNT ? MEASURE_SVC_MAX_AVERAGE_COUNT : count;
}

static esp_err_t load_count(const char *key, uint32_t *count)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");
    err = nvs_get_u32(handle, key, count);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_get_u32 failed");
    *count = clamp_count(*count);
    return ESP_OK;
}

esp_err_t measure_svc_average_init(void)
{
    ESP_RETURN_ON_ERROR(load_count(NVS_VOLTAGE_KEY, &s_counts[0]), TAG, "loading voltage average failed");
    ESP_RETURN_ON_ERROR(load_count(NVS_CURRENT_KEY, &s_counts[1]), TAG, "loading current average failed");
    return load_count(NVS_POWER_KEY, &s_counts[2]);
}

esp_err_t measure_svc_set_average_count(measure_kind_t kind, uint32_t count)
{
    uint32_t *stored_count;
    const char *key;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(kind_slot(kind, &stored_count, &key), TAG, "invalid measurement kind");
    count = clamp_count(count);
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_u32(handle, key, count);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        *stored_count = count;
    }
    return err;
}

esp_err_t measure_svc_get_average_count(measure_kind_t kind, uint32_t *count)
{
    uint32_t *stored_count;
    const char *key;
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!measure_svc_core_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(kind_slot(kind, &stored_count, &key), TAG, "invalid measurement kind");
    (void)key;
    *count = *stored_count;
    return ESP_OK;
}

esp_err_t measure_svc_read(measure_channel_t channel, measure_kind_t kind, uint32_t *value_u4)
{
    size_t copied_count = 0U;
    uint64_t total_u4 = 0U;
    uint32_t average_count;

    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((channel == MEASURE_CHANNEL_0) && (kind != MEASURE_KIND_VOLTAGE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((channel != MEASURE_CHANNEL_0) && (channel != MEASURE_CHANNEL_1)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((channel == MEASURE_CHANNEL_0) &&
        (measure_svc_core_get_config()->input_voltage_input == MEASURE_INPUT_UNUSED)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(measure_svc_get_average_count(kind, &average_count), TAG, "reading average count failed");
    ESP_RETURN_ON_ERROR(measure_svc_history_copy_latest_samples(
        channel, kind, average_count, s_samples, &copied_count), TAG, "copying samples failed");
    if (copied_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < copied_count; ++i) {
        total_u4 += s_samples[i].value_u4;
    }
    *value_u4 = (uint32_t)((total_u4 + (copied_count / 2U)) / copied_count);
    return ESP_OK;
}
