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

#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"

/**
 * @file measure_svc_calibration_runtime.h
 * @brief Private initialization and hot-path calibration application contract.
 */

/** @brief Load all active calibration targets and prepare transaction state. */
esp_err_t measure_svc_calibration_init(void);
/**
 * @brief Apply the active table selected by quantity and logical channel.
 * @return Calibrated u4 value, or the raw input if the target cannot be applied.
 */
uint32_t measure_svc_calibration_apply_target_u4(
    measure_kind_t kind, measure_channel_t channel, int16_t raw_code,
    uint16_t pga_full_scale_mv);
