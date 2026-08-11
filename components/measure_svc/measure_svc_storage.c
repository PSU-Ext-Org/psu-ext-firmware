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
 * @file measure_svc_storage.c
 * @brief Circular sample storage and calibration sample handoff.
 */

#include "measure_svc_storage.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MEASURE_SVC_CALIBRATION_TIMEOUT_TICKS pdMS_TO_TICKS(5000U)

static const char *TAG = "measure_store";

typedef struct {
    measure_svc_sample_t *samples;
    size_t capacity;
    size_t write_index;
    size_t count;
    uint32_t sequence;
} measure_svc_sample_ring_t;

typedef struct {
    bool active;
    bool ready;
    measure_channel_t channel;
    measure_kind_t kind;
    measure_input_t input;
    uint32_t after_sequence;
    uint32_t raw_voltage_u4;
} measure_svc_pending_calibration_t;

static measure_svc_sample_ring_t s_sample_rings[4] = {
    {
        .capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY,
    },
    {
        .capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY,
    },
    {
        .capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY,
    },
    {
        .capacity = MEASURE_SVC_MAX_SAMPLE_CAPACITY,
    },
};
static SemaphoreHandle_t s_measure_lock;
static SemaphoreHandle_t s_calibration_ready;
static measure_svc_pending_calibration_t s_pending_calibration;

/**
 * @brief Allocate one ring buffer from PSRAM if it is not already present.
 */
static esp_err_t measure_svc_storage_alloc_ring_samples(measure_svc_sample_ring_t *ring)
{
    if (ring == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ring->samples != NULL) {
        return ESP_OK;
    }

    ring->samples = heap_caps_calloc(
        ring->capacity,
        sizeof(measure_svc_sample_t),
        MALLOC_CAP_SPIRAM);
    if (ring->samples == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * @brief Release any partially allocated ring buffers after initialization failure.
 */
static void measure_svc_storage_free_ring_samples(void)
{
    for (size_t i = 0; i < (sizeof(s_sample_rings) / sizeof(s_sample_rings[0])); ++i) {
        if (s_sample_rings[i].samples != NULL) {
            heap_caps_free(s_sample_rings[i].samples);
            s_sample_rings[i].samples = NULL;
        }
        s_sample_rings[i].write_index = 0U;
        s_sample_rings[i].count = 0U;
        s_sample_rings[i].sequence = 0U;
    }
}

static esp_err_t measure_svc_storage_slot_to_index(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t *index)
{
    if (index == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((channel == MEASURE_CHANNEL_0) && (kind == MEASURE_KIND_VOLTAGE)) {
        *index = 0U;
        return ESP_OK;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        *index = 1U;
        return ESP_OK;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        *index = 2U;
        return ESP_OK;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_POWER)) {
        *index = 3U;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t measure_svc_storage_init(void)
{
    if (s_measure_lock == NULL) {
        s_measure_lock = xSemaphoreCreateMutex();
        if (s_measure_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_calibration_ready == NULL) {
        s_calibration_ready = xSemaphoreCreateBinary();
        if (s_calibration_ready == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const char *ring_names[] = {"ch0_voltage", "ch1_voltage", "ch1_current", "ch1_power"};
    for (size_t i = 0; i < (sizeof(s_sample_rings) / sizeof(s_sample_rings[0])); ++i) {
        esp_err_t err = measure_svc_storage_alloc_ring_samples(&s_sample_rings[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "allocating %s sample ring in PSRAM failed", ring_names[i]);
            measure_svc_storage_free_ring_samples();
            return err;
        }
    }

    ESP_LOGI(
        TAG,
        "sample rings allocated in PSRAM: %u bytes total",
        (unsigned)((sizeof(s_sample_rings) / sizeof(s_sample_rings[0])) *
                   MEASURE_SVC_MAX_SAMPLE_CAPACITY *
                   sizeof(measure_svc_sample_t)));

    return ESP_OK;
}

void measure_svc_storage_store_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t physical_input,
    uint32_t time_ms,
    uint32_t raw_value_u4,
    uint32_t stored_value_u4)
{
    size_t index;

    if ((measure_svc_storage_slot_to_index(channel, kind, &index) != ESP_OK) || (s_measure_lock == NULL)) {
        return;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    measure_svc_sample_ring_t *ring = &s_sample_rings[index];
    if (ring->samples == NULL) {
        xSemaphoreGive(s_measure_lock);
        return;
    }

    ring->samples[ring->write_index].time_ms = time_ms;
    ring->samples[ring->write_index].value_u4 = stored_value_u4;
    ring->write_index = (ring->write_index + 1U) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    }
    ring->sequence++;

    if ((kind != MEASURE_KIND_POWER) &&
        s_pending_calibration.active &&
        (channel == s_pending_calibration.channel) &&
        (kind == s_pending_calibration.kind) &&
        (physical_input == s_pending_calibration.input) &&
        !s_pending_calibration.ready &&
        (ring->sequence > s_pending_calibration.after_sequence)) {
        s_pending_calibration.raw_voltage_u4 = raw_value_u4;
        s_pending_calibration.ready = true;
        (void)xSemaphoreGive(s_calibration_ready);
    }

    xSemaphoreGive(s_measure_lock);
}

static void measure_svc_storage_sample_listener(
    const measure_svc_sample_event_t *event,
    void *context)
{
    (void)context;

    if (event == NULL) {
        return;
    }

    measure_svc_storage_store_sample(
        event->channel,
        event->kind,
        event->physical_input,
        event->time_ms,
        event->raw_value_u4,
        event->value_u4);
}

esp_err_t measure_svc_storage_register_listeners(void)
{
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(
            MEASURE_KIND_VOLTAGE,
            measure_svc_storage_sample_listener,
            NULL),
        TAG,
        "registering voltage storage listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(
            MEASURE_KIND_CURRENT,
            measure_svc_storage_sample_listener,
            NULL),
        TAG,
        "registering current storage listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(
            MEASURE_KIND_POWER,
            measure_svc_storage_sample_listener,
            NULL),
        TAG,
        "registering power storage listener failed");
    return ESP_OK;
}

esp_err_t measure_svc_storage_get_latest_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_svc_sample_t *sample)
{
    size_t index;

    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(measure_svc_storage_slot_to_index(channel, kind, &index), TAG, "invalid slot");
    if (s_measure_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const measure_svc_sample_ring_t *ring = &s_sample_rings[index];
    if ((ring->samples == NULL) || (ring->count == 0U)) {
        xSemaphoreGive(s_measure_lock);
        return ESP_ERR_INVALID_STATE;
    }

    size_t latest_index = (ring->write_index == 0U) ? (ring->capacity - 1U) : (ring->write_index - 1U);
    *sample = ring->samples[latest_index];
    xSemaphoreGive(s_measure_lock);
    return ESP_OK;
}

esp_err_t measure_svc_storage_copy_samples(
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
    ESP_RETURN_ON_ERROR(measure_svc_storage_slot_to_index(channel, kind, &index), TAG, "invalid slot");
    if (s_measure_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const measure_svc_sample_ring_t *ring = &s_sample_rings[index];
    if ((ring->samples == NULL) || (ring->count == 0U) || (start_offset >= ring->count) || (max_count == 0U)) {
        xSemaphoreGive(s_measure_lock);
        return ESP_OK;
    }

    size_t copy_count = ring->count - start_offset;
    if (copy_count > max_count) {
        copy_count = max_count;
    }

    const size_t oldest_index = (ring->write_index + ring->capacity - ring->count) % ring->capacity;
    for (size_t i = 0; i < copy_count; ++i) {
        const size_t source_index = (oldest_index + start_offset + i) % ring->capacity;
        samples[i] = ring->samples[source_index];
    }

    *copied_count = copy_count;
    xSemaphoreGive(s_measure_lock);
    return ESP_OK;
}

esp_err_t measure_svc_storage_copy_latest_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    size_t index;

    if ((copied_count == NULL) || ((max_count > 0U) && (samples == NULL))) {
        return ESP_ERR_INVALID_ARG;
    }

    *copied_count = 0U;
    ESP_RETURN_ON_ERROR(measure_svc_storage_slot_to_index(channel, kind, &index), TAG, "invalid slot");
    if (s_measure_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const measure_svc_sample_ring_t *ring = &s_sample_rings[index];
    if ((ring->samples == NULL) || (ring->count == 0U) || (max_count == 0U)) {
        xSemaphoreGive(s_measure_lock);
        return ESP_OK;
    }

    size_t copy_count = ring->count;
    if (copy_count > max_count) {
        copy_count = max_count;
    }

    const size_t start_offset = ring->count - copy_count;
    const size_t oldest_index = (ring->write_index + ring->capacity - ring->count) % ring->capacity;
    for (size_t i = 0; i < copy_count; ++i) {
        const size_t source_index = (oldest_index + start_offset + i) % ring->capacity;
        samples[i] = ring->samples[source_index];
    }

    *copied_count = copy_count;
    xSemaphoreGive(s_measure_lock);
    return ESP_OK;
}

esp_err_t measure_svc_storage_wait_for_fresh_raw_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t physical_input,
    uint32_t *value_u4)
{
    size_t index;

    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        measure_svc_storage_slot_to_index(channel, kind, &index),
        TAG,
        "invalid measurement slot");

    if ((kind == MEASURE_KIND_POWER) || (physical_input == MEASURE_INPUT_UNUSED)) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((s_measure_lock == NULL) || (s_calibration_ready == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_pending_calibration.active) {
        xSemaphoreGive(s_measure_lock);
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_calibration_ready, 0) == pdTRUE) {
    }

    s_pending_calibration.active = true;
    s_pending_calibration.ready = false;
    s_pending_calibration.channel = channel;
    s_pending_calibration.kind = kind;
    s_pending_calibration.input = physical_input;
    s_pending_calibration.after_sequence = s_sample_rings[index].sequence;
    xSemaphoreGive(s_measure_lock);

    if (xSemaphoreTake(s_calibration_ready, MEASURE_SVC_CALIBRATION_TIMEOUT_TICKS) != pdTRUE) {
        if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) == pdTRUE) {
            s_pending_calibration.active = false;
            s_pending_calibration.ready = false;
            xSemaphoreGive(s_measure_lock);
        }
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_measure_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_pending_calibration.ready) {
        s_pending_calibration.active = false;
        xSemaphoreGive(s_measure_lock);
        return ESP_ERR_INVALID_STATE;
    }

    *value_u4 = s_pending_calibration.raw_voltage_u4;
    s_pending_calibration.active = false;
    s_pending_calibration.ready = false;
    xSemaphoreGive(s_measure_lock);
    return ESP_OK;
}
