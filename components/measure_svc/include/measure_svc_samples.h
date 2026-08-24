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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"

/**
 * @file measure_svc_samples.h
 * @brief Sample events, averaging, latest-value reads, and history access.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_SVC_MIN_AVERAGE_COUNT 1U /**< Smallest read-average window. */
#define MEASURE_SVC_MAX_AVERAGE_COUNT 100U /**< Largest read-average window. */
#define MEASURE_SVC_DEFAULT_AVERAGE_COUNT 50U /**< Initial average window. */
#define MEASURE_SVC_MAX_SAMPLE_CAPACITY 1000U /**< Samples retained per ring. */
#define MEASURE_SVC_MAX_SAMPLE_LISTENERS 8U /**< Listeners allowed per kind. */

/** @brief Timestamped calibrated history value in u4. */
typedef struct {
    uint32_t time_ms; /**< Milliseconds from the ESP timer time base. */
    uint32_t value_u4; /**< Physical value multiplied by 10,000. */
} measure_svc_sample_t;

/** @brief Complete event published for one sampled or derived value. */
typedef struct {
    measure_kind_t kind; /**< Quantity represented by the event. */
    measure_channel_t channel; /**< Logical destination channel. */
    measure_input_t physical_input; /**< Source input, or unused if derived. */
    uint32_t time_ms; /**< Milliseconds from the ESP timer time base. */
    uint32_t raw_value_u4; /**< Uncalibrated provider value in u4. */
    int16_t raw_code; /**< Signed native ADC code before u4 conversion. */
    uint32_t value_u4; /**< Calibrated or derived value in u4. */
} measure_svc_sample_event_t;

/** @brief Listener invoked synchronously when an event of its kind is published. */
typedef void (*measure_svc_sample_listener_fn_t)(const measure_svc_sample_event_t *event, void *context);

/** @brief Register an event listener for one measurement kind. */
esp_err_t measure_svc_register_sample_listener(
    measure_kind_t kind, measure_svc_sample_listener_fn_t callback, void *context);
/** @brief Persist and apply the latest-value average count for a kind. */
esp_err_t measure_svc_set_average_count(measure_kind_t kind, uint32_t count);
/** @brief Read the configured latest-value average count for a kind. */
esp_err_t measure_svc_get_average_count(measure_kind_t kind, uint32_t *count);
/** @brief Average the newest configured number of calibrated history samples. */
esp_err_t measure_svc_read(measure_channel_t channel, measure_kind_t kind, uint32_t *value_u4);
/** @brief Copy voltage history starting at the newest-relative offset. */
esp_err_t measure_svc_copy_voltage_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count);
/** @brief Copy CH1 current history starting at the newest-relative offset. */
esp_err_t measure_svc_copy_current_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count);
/** @brief Copy CH1 derived-power history starting at the newest-relative offset. */
esp_err_t measure_svc_copy_power_samples(
    measure_channel_t channel, size_t start_offset, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count);

#ifdef __cplusplus
}
#endif
