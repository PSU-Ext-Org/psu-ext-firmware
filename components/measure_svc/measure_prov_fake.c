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
 * @file measure_prov_fake.c
 * @brief Fake/random raw measurement provider for development and fallback use.
 */

#include "measure_provider.h"

#include <limits.h>
#include <stdint.h>

#include "esp_random.h"

#define FAKE_VOLTAGE_MIN_MV 0U
#define FAKE_VOLTAGE_MAX_MV 30000U
#define FAKE_CURRENT_MIN_MA 1U
#define FAKE_CURRENT_MAX_MA 3000U

/**
 * @brief Generate a random integer in the inclusive range [`min`, `max`].
 *
 * @param min Minimum output value.
 * @param max Maximum output value.
 *
 * @return Random integer in the requested range.
 */
static uint32_t measure_prov_fake_random_range(uint32_t min, uint32_t max)
{
    uint32_t span = (max - min) + 1U;
    return min + (esp_random() % span);
}

/**
 * @brief Produce a fake raw fixed-point reading for the requested input and kind.
 *
 * Inputs are validated even though the fake backend currently treats them the
 * same, so future providers can keep the same call contract.
 *
 * @param input Measurement input being sampled.
 * @param kind Measurement quantity being sampled.
 * @param value_u4 Output pointer receiving the fake value scaled by 10,000.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if any argument is invalid
 */
static esp_err_t measure_prov_fake_read_raw(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((input < MEASURE_INPUT_ADS1115_AIN0) || (input > MEASURE_INPUT_ADS1115_AIN3)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (kind == MEASURE_KIND_VOLTAGE) {
        *value_u4 = measure_prov_fake_random_range(
            FAKE_VOLTAGE_MIN_MV,
            FAKE_VOLTAGE_MAX_MV) * 10U;
        return ESP_OK;
    }

    if (kind == MEASURE_KIND_CURRENT) {
        *value_u4 = measure_prov_fake_random_range(
            FAKE_CURRENT_MIN_MA,
            FAKE_CURRENT_MAX_MA) * 10U;
        return ESP_OK;
    }

    if (kind == MEASURE_KIND_POWER) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_ERR_INVALID_ARG;
}

static uint32_t measure_prov_fake_raw_code_to_u4(int16_t raw_code)
{
    if (raw_code <= 0) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)(uint16_t)raw_code * 20480U + 16383U) / 32767U);
}

static esp_err_t measure_prov_fake_read_raw_sample(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4,
    int16_t *raw_code)
{
    if ((value_u4 == NULL) || (raw_code == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = measure_prov_fake_read_raw(input, kind, value_u4);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t code = ((uint64_t)*value_u4 * 32767U + 10240U) / 20480U;
    if (code > INT16_MAX) {
        code = INT16_MAX;
    }
    *raw_code = (int16_t)code;
    return ESP_OK;
}

static esp_err_t measure_prov_fake_read(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    return measure_prov_fake_read_raw(input, kind, value_u4);
}

static const measure_provider_t s_fake_prov = {
    .name = "FAKE_RANDOM",
    .read_value_u4 = measure_prov_fake_read,
    .read_raw_value_u4 = measure_prov_fake_read_raw,
    .read_raw_sample = measure_prov_fake_read_raw_sample,
    .raw_code_to_value_u4 = measure_prov_fake_raw_code_to_u4,
};

const measure_provider_t *measure_prov_fake_get(void)
{
    return &s_fake_prov;
}
