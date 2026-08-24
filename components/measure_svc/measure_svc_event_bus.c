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

#include "measure_svc_event_bus.h"

#include <stddef.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "measure_events";

typedef struct {
    measure_svc_sample_listener_fn_t callback;
    void *context;
} measure_svc_listener_t;

static SemaphoreHandle_t s_listener_lock;
static measure_svc_listener_t s_listeners[3][MEASURE_SVC_MAX_SAMPLE_LISTENERS];
static size_t s_listener_counts[3];

static esp_err_t kind_index(measure_kind_t kind, size_t *index)
{
    if ((index == NULL) || (kind < MEASURE_KIND_VOLTAGE) || (kind > MEASURE_KIND_POWER)) {
        return ESP_ERR_INVALID_ARG;
    }
    *index = (size_t)kind;
    return ESP_OK;
}

esp_err_t measure_svc_event_bus_init(void)
{
    if (s_listener_lock == NULL) {
        s_listener_lock = xSemaphoreCreateMutex();
        if (s_listener_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t measure_svc_register_sample_listener(
    measure_kind_t kind,
    measure_svc_sample_listener_fn_t callback,
    void *context)
{
    size_t index;
    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "listener callback is null");
    ESP_RETURN_ON_ERROR(kind_index(kind, &index), TAG, "invalid listener kind");
    ESP_RETURN_ON_ERROR(measure_svc_event_bus_init(), TAG, "initializing listener registry failed");

    if (xSemaphoreTake(s_listener_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < s_listener_counts[index]; ++i) {
        if ((s_listeners[index][i].callback == callback) &&
            (s_listeners[index][i].context == context)) {
            xSemaphoreGive(s_listener_lock);
            return ESP_OK;
        }
    }

    if (s_listener_counts[index] >= MEASURE_SVC_MAX_SAMPLE_LISTENERS) {
        xSemaphoreGive(s_listener_lock);
        return ESP_ERR_NO_MEM;
    }

    s_listeners[index][s_listener_counts[index]].callback = callback;
    s_listeners[index][s_listener_counts[index]].context = context;
    ++s_listener_counts[index];
    xSemaphoreGive(s_listener_lock);
    return ESP_OK;
}

void measure_svc_event_bus_publish(const measure_svc_sample_event_t *event)
{
    measure_svc_listener_t callbacks[MEASURE_SVC_MAX_SAMPLE_LISTENERS];
    size_t index;
    size_t count;

    if ((event == NULL) || (s_listener_lock == NULL) ||
        (kind_index(event->kind, &index) != ESP_OK)) {
        return;
    }
    if (xSemaphoreTake(s_listener_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    count = s_listener_counts[index];
    for (size_t i = 0; i < count; ++i) {
        callbacks[i] = s_listeners[index][i];
    }
    xSemaphoreGive(s_listener_lock);

    for (size_t i = 0; i < count; ++i) {
        callbacks[i].callback(event, callbacks[i].context);
    }
}
