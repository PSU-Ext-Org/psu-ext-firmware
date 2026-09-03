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

#include "measure_svc_calibration_capture.h"

#include <limits.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "measure_svc_calibration_capture_config.h"
#include "measure_svc_samples.h"

static const char *TAG = "measure_cal_capture";

typedef struct {
    measure_channel_t channel;
    measure_kind_t kind;
    measure_input_t input;
} measure_svc_cal_capture_target_t;

typedef struct {
    bool active;
    bool ready;
    measure_svc_cal_capture_target_t target;
    measure_svc_cal_capture_window_t window;
    int16_t accepted_mean;
    uint32_t last_source_generation;
} measure_svc_cal_capture_request_t;

static SemaphoreHandle_t s_capture_lock;
static SemaphoreHandle_t s_capture_ready;
static measure_svc_cal_capture_request_t s_request;

void measure_svc_cal_capture_window_reset(measure_svc_cal_capture_window_t *window)
{
    if (window == NULL) {
        return;
    }
    *window = (measure_svc_cal_capture_window_t){
        .discard_remaining = MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES,
        .code_min = INT16_MAX,
        .code_max = INT16_MIN,
    };
}

bool measure_svc_cal_capture_window_add(measure_svc_cal_capture_window_t *window, int16_t raw_code)
{
    if (window == NULL) {
        return false;
    }
    if (window->discard_remaining > 0U) {
        --window->discard_remaining;
        return false;
    }
    if (window->sample_count >= MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES) {
        return true;
    }
    if (raw_code < window->code_min) {
        window->code_min = raw_code;
    }
    if (raw_code > window->code_max) {
        window->code_max = raw_code;
    }
    window->code_sum += raw_code;
    ++window->sample_count;
    return window->sample_count == MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES;
}

bool measure_svc_cal_capture_window_is_stable(const measure_svc_cal_capture_window_t *window)
{
    return (window != NULL) &&
        (window->sample_count == MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES) &&
        (((int32_t)window->code_max - (int32_t)window->code_min) <=
            MEASURE_SVC_CAL_STABILITY_P2P_MAX_CODES);
}

int16_t measure_svc_cal_capture_window_mean(const measure_svc_cal_capture_window_t *window)
{
    if ((window == NULL) || (window->sample_count == 0U)) {
        return 0;
    }
    const int64_t half = (int64_t)(window->sample_count / 2U);
    return window->code_sum >= 0
        ? (int16_t)((window->code_sum + half) / window->sample_count)
        : (int16_t)((window->code_sum - half) / window->sample_count);
}

esp_err_t measure_svc_calibration_capture_init(void)
{
    if (s_capture_lock == NULL) {
        s_capture_lock = xSemaphoreCreateMutex();
        if (s_capture_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_capture_ready == NULL) {
        s_capture_ready = xSemaphoreCreateBinary();
        if (s_capture_ready == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void capture_listener(const measure_svc_sample_event_t *event, void *context)
{
    (void)context;
    if ((event == NULL) || (event->kind == MEASURE_KIND_POWER) ||
        (s_capture_lock == NULL) || (xSemaphoreTake(s_capture_lock, portMAX_DELAY) != pdTRUE)) {
        return;
    }

    if (s_request.active && !s_request.ready &&
        (event->channel == s_request.target.channel) &&
        (event->kind == s_request.target.kind) &&
        (event->physical_input == s_request.target.input)) {
        if ((event->source_generation != 0U) &&
            (event->source_generation == s_request.last_source_generation)) {
            xSemaphoreGive(s_capture_lock);
            return;
        }
        s_request.last_source_generation = event->source_generation;
        if (measure_svc_cal_capture_window_add(&s_request.window, event->raw_code)) {
            if (measure_svc_cal_capture_window_is_stable(&s_request.window)) {
                s_request.accepted_mean = measure_svc_cal_capture_window_mean(&s_request.window);
                s_request.ready = true;
                (void)xSemaphoreGive(s_capture_ready);
            } else {
                measure_svc_cal_capture_window_reset(&s_request.window);
            }
        }
    }
    xSemaphoreGive(s_capture_lock);
}

esp_err_t measure_svc_calibration_capture_register_listener(void)
{
    ESP_RETURN_ON_ERROR(measure_svc_register_sample_listener(
        MEASURE_KIND_VOLTAGE, capture_listener, NULL), TAG, "registering voltage capture failed");
    return measure_svc_register_sample_listener(
        MEASURE_KIND_CURRENT, capture_listener, NULL);
}

esp_err_t measure_svc_calibration_capture_wait(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t physical_input,
    int16_t *raw_code)
{
    if ((raw_code == NULL) || (kind == MEASURE_KIND_POWER) ||
        (physical_input == MEASURE_INPUT_UNUSED)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_capture_lock == NULL) || (s_capture_ready == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_capture_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_request.active) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    while (xSemaphoreTake(s_capture_ready, 0) == pdTRUE) {
    }
    s_request.active = true;
    s_request.ready = false;
    s_request.target = (measure_svc_cal_capture_target_t){channel, kind, physical_input};
    s_request.last_source_generation = 0U;
    measure_svc_cal_capture_window_reset(&s_request.window);
    xSemaphoreGive(s_capture_lock);

    if (xSemaphoreTake(s_capture_ready, MEASURE_SVC_CAL_CAPTURE_TIMEOUT_TICKS) != pdTRUE) {
        if (xSemaphoreTake(s_capture_lock, portMAX_DELAY) == pdTRUE) {
            s_request.active = false;
            s_request.ready = false;
            xSemaphoreGive(s_capture_lock);
        }
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(s_capture_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_request.ready) {
        s_request.active = false;
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    *raw_code = s_request.accepted_mean;
    s_request.active = false;
    s_request.ready = false;
    xSemaphoreGive(s_capture_lock);
    return ESP_OK;
}
