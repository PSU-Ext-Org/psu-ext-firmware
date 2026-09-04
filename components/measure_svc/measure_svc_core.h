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
#include "measure_svc.h"

/**
 * @file measure_svc_core.h
 * @brief Private routing and provider-access contract for service modules.
 */

/** @brief Resolve a logical channel/kind target to its configured physical input. */
esp_err_t measure_svc_core_get_physical_input(
    measure_channel_t channel, measure_kind_t kind, measure_input_t *input);
/** @brief Return the immutable active service configuration. */
const measure_svc_config_t *measure_svc_core_get_config(void);
/** @brief Report whether composition-root initialization has completed. */
bool measure_svc_core_is_initialized(void);
/** @brief Read the latest provider sample and its unique conversion ID. */
esp_err_t measure_svc_core_read_raw_sample(
    measure_input_t input, measure_kind_t kind, uint32_t *value_u4, int16_t *raw_code,
    uint32_t *source_generation, uint16_t *pga_full_scale_mv);
/** @brief Convert a signed native provider code to uncalibrated u4 once. */
esp_err_t measure_svc_core_raw_code_to_u4(
    int16_t raw_code, uint16_t pga_full_scale_mv, uint32_t *value_u4);
