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

/**
 * @file measure_types.h
 * @brief Shared logical channel, physical input, and measurement-kind types.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Logical measurement channels exposed by the service and SCPI API. */
typedef enum {
    MEASURE_CHANNEL_0 = 0, /**< Input-voltage channel when configured. */
    MEASURE_CHANNEL_1 = 1, /**< PSU output voltage/current/power channel. */
} measure_channel_t;

/** @brief Physical inputs supported by measurement providers. */
typedef enum {
    MEASURE_INPUT_UNUSED = -1,      /**< Logical measurement is disabled. */
    MEASURE_INPUT_ADS1115_AIN0 = 0, /**< ADS1115 single-ended input AIN0. */
    MEASURE_INPUT_ADS1115_AIN1 = 1, /**< ADS1115 single-ended input AIN1. */
    MEASURE_INPUT_ADS1115_AIN2 = 2, /**< ADS1115 single-ended input AIN2. */
    MEASURE_INPUT_ADS1115_AIN3 = 3, /**< ADS1115 single-ended input AIN3. */
} measure_input_t;

/** @brief Quantity represented by a sample or calibration target. */
typedef enum {
    MEASURE_KIND_VOLTAGE = 0, /**< Voltage in volts, represented in u4. */
    MEASURE_KIND_CURRENT = 1, /**< Current in amperes, represented in u4. */
    MEASURE_KIND_POWER = 2,   /**< Derived power in watts, represented in u4. */
} measure_kind_t;

#ifdef __cplusplus
}
#endif
