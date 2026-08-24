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

#include "measure_svc_history.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "measure_history";

typedef struct {
    measure_svc_sample_t *samples;
    size_t capacity;
    size_t write_index;
    size_t count;
} measure_svc_sample_ring_t;

static measure_svc_sample_ring_t s_rings[4] = {
    {.capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY},
    {.capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY},
    {.capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY},
    {.capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY},
};
static SemaphoreHandle_t s_history_lock;

static esp_err_t slot_index(measure_channel_t channel, measure_kind_t kind, size_t *index)
{
    if (index == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((channel == MEASURE_CHANNEL_0) && (kind == MEASURE_KIND_VOLTAGE)) {
        *index = 0U;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        *index = 1U;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        *index = 2U;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_POWER)) {
        *index = 3U;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void free_rings(void)
{
    for (size_t i = 0; i < 4U; ++i) {
        if (s_rings[i].samples != NULL) {
            heap_caps_free(s_rings[i].samples);
            s_rings[i].samples = NULL;
        }
        s_rings[i].write_index = 0U;
        s_rings[i].count = 0U;
    }
}

esp_err_t measure_svc_history_init(void)
{
    if (s_history_lock == NULL) {
        s_history_lock = xSemaphoreCreateMutex();
        if (s_history_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    for (size_t i = 0; i < 4U; ++i) {
        if (s_rings[i].samples == NULL) {
            s_rings[i].samples = heap_caps_calloc(
                s_rings[i].capacity, sizeof(measure_svc_sample_t), MALLOC_CAP_SPIRAM);
            if (s_rings[i].samples == NULL) {
                free_rings();
                return ESP_ERR_NO_MEM;
            }
        }
    }

    ESP_LOGI(TAG, "sample rings allocated in PSRAM: %u bytes total",
        (unsigned)(4U * MEASURE_SVC_MAX_SAMPLE_CAPACITY * sizeof(measure_svc_sample_t)));
    return ESP_OK;
}

static void history_listener(const measure_svc_sample_event_t *event, void *context)
{
    size_t index;
    (void)context;

    if ((event == NULL) || (slot_index(event->channel, event->kind, &index) != ESP_OK) ||
        (s_history_lock == NULL) || (xSemaphoreTake(s_history_lock, portMAX_DELAY) != pdTRUE)) {
        return;
    }

    measure_svc_sample_ring_t *ring = &s_rings[index];
    if (ring->samples != NULL) {
        ring->samples[ring->write_index] = (measure_svc_sample_t){
            .time_ms = event->time_ms,
            .value_u4 = event->value_u4,
        };
        ring->write_index = (ring->write_index + 1U) % ring->capacity;
        if (ring->count < ring->capacity) {
            ++ring->count;
        }
    }
    xSemaphoreGive(s_history_lock);
}

esp_err_t measure_svc_history_register_listeners(void)
{
    ESP_RETURN_ON_ERROR(measure_svc_register_sample_listener(
        MEASURE_KIND_VOLTAGE, history_listener, NULL), TAG, "registering voltage history failed");
    ESP_RETURN_ON_ERROR(measure_svc_register_sample_listener(
        MEASURE_KIND_CURRENT, history_listener, NULL), TAG, "registering current history failed");
    return measure_svc_register_sample_listener(MEASURE_KIND_POWER, history_listener, NULL);
}

esp_err_t measure_svc_history_copy_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    size_t index;
    if ((copied_count == NULL) || ((max_count > 0U) && (samples == NULL))) {
        return ESP_ERR_INVALID_ARG;
    }
    *copied_count = 0U;
    ESP_RETURN_ON_ERROR(slot_index(channel, kind, &index), TAG, "invalid history slot");
    if (s_history_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_history_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const measure_svc_sample_ring_t *ring = &s_rings[index];
    if ((ring->samples != NULL) && (start_offset < ring->count) && (max_count > 0U)) {
        size_t count = ring->count - start_offset;
        if (count > max_count) {
            count = max_count;
        }
        const size_t oldest = (ring->write_index + ring->capacity - ring->count) % ring->capacity;
        for (size_t i = 0; i < count; ++i) {
            samples[i] = ring->samples[(oldest + start_offset + i) % ring->capacity];
        }
        *copied_count = count;
    }
    xSemaphoreGive(s_history_lock);
    return ESP_OK;
}

esp_err_t measure_svc_history_copy_latest_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    size_t index;
    ESP_RETURN_ON_ERROR(slot_index(channel, kind, &index), TAG, "invalid history slot");
    if (s_history_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_history_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const size_t available = s_rings[index].count;
    xSemaphoreGive(s_history_lock);
    const size_t count = available < max_count ? available : max_count;
    return measure_svc_history_copy_samples(channel, kind, available - count, count, samples, copied_count);
}

esp_err_t measure_svc_copy_voltage_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count)
{
    if ((channel != MEASURE_CHANNEL_0) && (channel != MEASURE_CHANNEL_1)) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_history_copy_samples(
        channel, MEASURE_KIND_VOLTAGE, start_offset, max_count, samples, copied_count);
}

esp_err_t measure_svc_copy_current_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count)
{
    if (channel != MEASURE_CHANNEL_1) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_history_copy_samples(
        channel, MEASURE_KIND_CURRENT, start_offset, max_count, samples, copied_count);
}

esp_err_t measure_svc_copy_power_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count)
{
    if (channel != MEASURE_CHANNEL_1) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_history_copy_samples(
        channel, MEASURE_KIND_POWER, start_offset, max_count, samples, copied_count);
}
