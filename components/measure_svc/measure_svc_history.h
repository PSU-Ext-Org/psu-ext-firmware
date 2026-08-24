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

#include "esp_err.h"
#include "measure_svc_samples.h"

/**
 * @file measure_svc_history.h
 * @brief Private PSRAM-backed calibrated sample-history contract.
 */

/** @brief Allocate and initialize the voltage, current, and power rings. */
esp_err_t measure_svc_history_init(void);
/** @brief Register history consumers on the sample event bus. */
esp_err_t measure_svc_history_register_listeners(void);
/**
 * @brief Copy samples from a history ring using a newest-relative offset.
 * @param channel Logical channel to inspect.
 * @param kind Voltage, current, or power history.
 * @param start_offset Number of newest samples to skip.
 * @param max_count Capacity of @p samples.
 * @param samples Destination array.
 * @param copied_count Number of samples written.
 */
esp_err_t measure_svc_history_copy_samples(
    measure_channel_t channel, measure_kind_t kind, size_t start_offset,
    size_t max_count, measure_svc_sample_t *samples, size_t *copied_count);
/** @brief Copy up to @p max_count newest samples in chronological order. */
esp_err_t measure_svc_history_copy_latest_samples(
    measure_channel_t channel, measure_kind_t kind, size_t max_count,
    measure_svc_sample_t *samples, size_t *copied_count);
