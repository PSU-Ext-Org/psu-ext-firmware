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

/** @brief Validate the compile-time ADS1115 autorange policy. */
bool measure_prov_ads1115_range_config_validate(void);

/** @brief Select at most one adjacent range from one ADC-side voltage sample. */
esp_err_t measure_prov_ads1115_range_select(
    uint8_t current_index, uint32_t adc_voltage_u4, uint8_t *next_index);

/** @brief Return the conversions discarded before publishing at one PGA. */
uint8_t measure_prov_ads1115_settling_conversions(uint16_t pga_full_scale_mv);
