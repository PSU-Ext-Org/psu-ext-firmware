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
 * @file measure_provider.h
 * @brief Measurement provider contract for channel voltage/current readings.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEASURE_CHANNEL_0 = 0,
    MEASURE_CHANNEL_1 = 1,
} measure_channel_t;

typedef enum {
    MEASURE_INPUT_UNUSED = -1,
    MEASURE_INPUT_ADS1115_AIN0 = 0,
    MEASURE_INPUT_ADS1115_AIN1 = 1,
    MEASURE_INPUT_ADS1115_AIN2 = 2,
    MEASURE_INPUT_ADS1115_AIN3 = 3,
} measure_input_t;

typedef enum {
    MEASURE_KIND_VOLTAGE = 0,
    MEASURE_KIND_CURRENT = 1,
    MEASURE_KIND_POWER = 2,
} measure_kind_t;

typedef struct {
    const char *name;
    esp_err_t (*read_value_u4)(
        measure_input_t input,
        measure_kind_t kind,
        uint32_t *value_u4);
    esp_err_t (*read_raw_value_u4)(
        measure_input_t input,
        measure_kind_t kind,
        uint32_t *value_u4);
} measure_provider_t;

#ifdef __cplusplus
}
#endif
