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

#include "measure_svc_relative.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"
#include "measure_svc_coordination.h"
#include "esp_check.h"
#include <stddef.h>
#include <string.h>

static const char *TAG = "measure_rel";
static uint32_t s_relative_u4[3];

static uint32_t *relative_offset(measure_kind_t kind, measure_channel_t channel)
{
    if (kind == MEASURE_KIND_VOLTAGE && channel == MEASURE_CHANNEL_0) {
        return &s_relative_u4[0];
    }
    if (kind == MEASURE_KIND_VOLTAGE && channel == MEASURE_CHANNEL_1) {
        return &s_relative_u4[1];
    }
    if (kind == MEASURE_KIND_CURRENT && channel == MEASURE_CHANNEL_1) {
        return &s_relative_u4[2];
    }
    return NULL;
}

void measure_svc_relative_clear_locked(void)
{
    memset(s_relative_u4, 0, sizeof(s_relative_u4));
}

esp_err_t measure_svc_set_relative_u4(
    measure_channel_t channel, measure_kind_t kind, uint32_t offset_u4)
{
    uint32_t *offset = relative_offset(kind, channel);
    if (offset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    bool active;
    ESP_RETURN_ON_ERROR(measure_svc_coordination_begin(&active), TAG, "taking coordination lock failed");
    if ((offset_u4 != 0U) && active) {
        measure_svc_coordination_end();
        return ESP_ERR_INVALID_STATE;
    }
    *offset = offset_u4;
    measure_svc_coordination_end();
    return ESP_OK;
}

esp_err_t measure_svc_get_relative_u4(
    measure_channel_t channel, measure_kind_t kind, uint32_t *offset_u4)
{
    uint32_t *offset = relative_offset(kind, channel);
    if ((offset == NULL) || (offset_u4 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    bool active;
    ESP_RETURN_ON_ERROR(measure_svc_coordination_begin(&active), TAG, "taking coordination lock failed");
    *offset_u4 = *offset;
    measure_svc_coordination_end();
    return ESP_OK;
}

esp_err_t measure_svc_read_relative(
    measure_channel_t channel, measure_kind_t kind, uint32_t *value_u4)
{
    uint32_t *offset = relative_offset(kind, channel);
    if ((offset == NULL) || (value_u4 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    bool active;
    ESP_RETURN_ON_ERROR(measure_svc_coordination_begin(&active), TAG, "taking coordination lock failed");
    esp_err_t err = measure_svc_read(channel, kind, value_u4);
    if (err == ESP_OK) {
        *value_u4 = *value_u4 > *offset ? *value_u4 - *offset : 0U;
    }
    measure_svc_coordination_end();
    return err;
}
