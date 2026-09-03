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
#include "measure_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Measurement-provider operations used by the service core.
 *
 * Values use unsigned u4 fixed point (physical unit multiplied by 10,000).
 * Native ADC codes remain signed so calibration capture can average before
 * converting and rounding to u4.
 */
typedef struct {
    const char *name; /**< Stable human-readable provider name. */
    /** Read one provider value in u4. */
    esp_err_t (*read_value_u4)(
        measure_input_t input,
        measure_kind_t kind,
        uint32_t *value_u4);
    /** Read the latest uncalibrated provider value in u4. */
    esp_err_t (*read_raw_value_u4)(
        measure_input_t input,
        measure_kind_t kind,
        uint32_t *value_u4);
    /** Read the latest ADC sample and its unique provider conversion ID. */
    esp_err_t (*read_raw_sample)(
        measure_input_t input,
        measure_kind_t kind,
        uint32_t *value_u4,
        int16_t *raw_code,
        uint32_t *source_generation);
    /** Convert a native ADC code to the provider's raw u4 representation. */
    uint32_t (*raw_code_to_value_u4)(int16_t raw_code);
} measure_provider_t;

/**
 * @brief Replace the active measurement provider.
 * @param provider Provider whose name and callbacks all remain valid for the
 * lifetime of the service.
 * @return `ESP_OK`, or `ESP_ERR_INVALID_ARG` if a required member is missing.
 */
esp_err_t measure_svc_set_prov(const measure_provider_t *provider);

/** @brief Initialize the production ADS1115 provider. */
esp_err_t measure_prov_ads1115_init(void);
/** @brief Return the production ADS1115 provider contract. */
const measure_provider_t *measure_prov_ads1115_get(void);
/** @brief Persist and activate one supported ADS1115 conversion rate. */
esp_err_t measure_prov_ads1115_set_data_rate_sps(uint16_t sps);
/** @brief Return the active ADS1115 conversion rate. */
esp_err_t measure_prov_ads1115_get_data_rate_sps(uint16_t *sps);
/** @brief Return the synthetic provider used by tests and diagnostics. */
const measure_provider_t *measure_prov_fake_get(void);

#ifdef __cplusplus
}
#endif
