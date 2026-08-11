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
 * @file measure_svc_storage.h
 * @brief Component-private sample storage helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "measure_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create storage locks and calibration wait primitives.
 */
esp_err_t measure_svc_storage_init(void);

/**
 * @brief Register storage callbacks for voltage, current, and power sample events.
 */
esp_err_t measure_svc_storage_register_listeners(void);

/**
 * @brief Store one sample and optionally satisfy a pending calibration wait.
 *
 * Samples are stored in the ring selected by @p kind. Only voltage/current
 * events may satisfy a pending calibration wait because calibration remains tied
 * to physical ADC channels. @p raw_value_u4 is transient and is used only when
 * a calibration command is waiting for a raw ADC point.
 */
void measure_svc_storage_store_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t physical_input,
    uint32_t time_ms,
    uint32_t raw_value_u4,
    uint32_t stored_value_u4);

/**
 * @brief Copy the newest stored sample for one logical channel/kind.
 */
esp_err_t measure_svc_storage_get_latest_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_svc_sample_t *sample);

/**
 * @brief Copy stored samples in oldest-to-newest order for one logical channel/kind.
 */
esp_err_t measure_svc_storage_copy_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count);

/**
 * @brief Copy the newest stored samples in oldest-to-newest order for one logical channel/kind.
 */
esp_err_t measure_svc_storage_copy_latest_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count);

/**
 * @brief Wait for the next raw sample for a measurement kind and physical input.
 *
 * Calibration uses this path because calibration points must remain raw ADC
 * voltage to actual-value pairs, even though the circular buffer stores
 * calibrated values.
 */
esp_err_t measure_svc_storage_wait_for_fresh_raw_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t physical_input,
    uint32_t *value_u4);

#ifdef __cplusplus
}
#endif
