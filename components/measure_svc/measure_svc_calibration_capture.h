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

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"

/**
 * @file measure_svc_calibration_capture.h
 * @brief Private native-code calibration-window state machine and event capture.
 */

/** @brief State accumulated for one discard-and-collect capture attempt. */
typedef struct {
    uint8_t discard_remaining; /**< Initial unique conversions still to ignore. */
    uint8_t sample_count; /**< Native codes included in the sum. */
    int64_t code_sum; /**< Signed sum used to avoid overflow and early rounding. */
    int16_t code_min; /**< Smallest collected native code. */
    int16_t code_max; /**< Largest collected native code. */
} measure_svc_cal_capture_window_t;

/** @brief Reset a window to the configured discard phase. */
void measure_svc_cal_capture_window_reset(measure_svc_cal_capture_window_t *window);
/** @brief Consume one native code and report whether the window is complete. */
bool measure_svc_cal_capture_window_add(measure_svc_cal_capture_window_t *window, int16_t raw_code);
/** @brief Test whether a complete window meets the peak-to-peak limit. */
bool measure_svc_cal_capture_window_is_stable(const measure_svc_cal_capture_window_t *window);
/** @brief Return the signed mean rounded to the nearest native code. */
int16_t measure_svc_cal_capture_window_mean(const measure_svc_cal_capture_window_t *window);

/** @brief Initialize capture synchronization state. */
esp_err_t measure_svc_calibration_capture_init(void);
/** @brief Subscribe the capture state machine to voltage and current events. */
esp_err_t measure_svc_calibration_capture_register_listener(void);
/**
 * @brief Wait for a stable window matching one exact logical and physical target.
 * @param channel Logical channel required in incoming events.
 * @param kind Measurement kind required in incoming events.
 * @param physical_input Physical input required in incoming events.
 * @param raw_code Accepted rounded native-code mean.
 * @param pga_full_scale_mv PGA used for every sample in the accepted window.
 * @return `ESP_OK`, `ESP_ERR_TIMEOUT`, or a state/argument error.
 */
esp_err_t measure_svc_calibration_capture_wait(
    measure_channel_t channel, measure_kind_t kind, measure_input_t physical_input,
    int16_t *raw_code, uint16_t *pga_full_scale_mv);
