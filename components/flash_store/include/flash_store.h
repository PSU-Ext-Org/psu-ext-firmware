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
 * @file flash_store.h
 * @brief External SPI flash storage helper.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and probe the external SPI flash device.
 *
 * The current implementation validates that the chip responds, registers a
 * private external-flash partition, validates the configured region layout,
 * and scans voltage/current measurement logs.
 *
 * @return
 * - `ESP_OK` if the external flash was detected
 * - Any SPI bus or flash driver error from the failed initialization step
 */
esp_err_t flash_store_init(void);

#define FLASH_STORE_MEASUREMENT_VALUE_U6_MAX 99999999U

/**
 * @brief One unsigned measurement sample.
 *
 * @param unix_time_ms Unix epoch timestamp in milliseconds.
 * @param value_u6 Fixed-point value scaled by 1,000,000.
 */
typedef struct {
    uint64_t unix_time_ms;
    uint32_t value_u6;
} flash_store_measurement_t;

/**
 * @brief Append one voltage measurement to the voltage log.
 *
 * The value must use unsigned fixed-point scaling where:
 *
 * @code
 * value_u6 = voltage * 1000000
 * @endcode
 *
 * For example, `12.345678 V` is stored as `12345678`.
 *
 * @code
 * flash_store_measurement_t voltage = {
 *     .unix_time_ms = 1777809600123ULL,
 *     .value_u6 = 12345678U,
 * };
 *
 * esp_err_t err = flash_store_voltage_append(&voltage);
 * if (err != ESP_OK) {
 *     // Handle append failure.
 * }
 * @endcode
 *
 * @param measurement Measurement to append.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p measurement is NULL, timestamp is zero, or value is out of range
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized or the voltage log is not writable
 * - `ESP_ERR_NO_MEM` if the voltage log is full
 * - Any lower-level partition/flash write error
 */
esp_err_t flash_store_voltage_append(const flash_store_measurement_t *measurement);

/**
 * @brief Append one current measurement to the current log.
 *
 * The value uses the same unsigned fixed-point `value * 1000000` scaling as
 * voltage measurements.
 *
 * @param measurement Measurement to append.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p measurement is NULL, timestamp is zero, or value is out of range
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized or the current log is not writable
 * - `ESP_ERR_NO_MEM` if the current log is full
 * - Any lower-level partition/flash write error
 */
esp_err_t flash_store_current_append(const flash_store_measurement_t *measurement);

/**
 * @brief Read one voltage measurement by zero-based index.
 *
 * Use `flash_store_voltage_count()` to discover how many samples are available,
 * then read each index sequentially.
 *
 * @code
 * uint32_t count = 0;
 * ESP_ERROR_CHECK(flash_store_voltage_count(&count));
 *
 * for (uint32_t i = 0; i < count; ++i) {
 *     flash_store_measurement_t voltage;
 *     ESP_ERROR_CHECK(flash_store_voltage_read(i, &voltage));
 *
 *     printf(
 *         "V[%lu] time=%llu value=%lu.%06lu\n",
 *         (unsigned long)i,
 *         (unsigned long long)voltage.unix_time_ms,
 *         (unsigned long)(voltage.value_u6 / 1000000U),
 *         (unsigned long)(voltage.value_u6 % 1000000U));
 * }
 * @endcode
 *
 * @param index Zero-based voltage measurement index.
 * @param measurement Output pointer receiving the decoded measurement.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p measurement is NULL
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 * - `ESP_ERR_NOT_FOUND` if @p index is outside the stored voltage count
 * - `ESP_ERR_INVALID_CRC` if the stored record is corrupt
 * - Any lower-level partition/flash read error
 */
esp_err_t flash_store_voltage_read(uint32_t index, flash_store_measurement_t *measurement);

/**
 * @brief Read one current measurement by zero-based index.
 *
 * @param index Zero-based current measurement index.
 * @param measurement Output pointer receiving the decoded measurement.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p measurement is NULL
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 * - `ESP_ERR_NOT_FOUND` if @p index is outside the stored current count
 * - `ESP_ERR_INVALID_CRC` if the stored record is corrupt
 * - Any lower-level partition/flash read error
 */
esp_err_t flash_store_current_read(uint32_t index, flash_store_measurement_t *measurement);

/**
 * @brief Return the number of stored voltage measurements.
 *
 * @param count Output pointer receiving the number of valid voltage records.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p count is NULL
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 */
esp_err_t flash_store_voltage_count(uint32_t *count);

/**
 * @brief Return the number of stored current measurements.
 *
 * @param count Output pointer receiving the number of valid current records.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p count is NULL
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 */
esp_err_t flash_store_current_count(uint32_t *count);

/**
 * @brief Erase all stored voltage measurements.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 * - Any lower-level partition/flash erase error
 */
esp_err_t flash_store_voltage_clear(void);

/**
 * @brief Erase all stored current measurements.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_STATE` if flash storage is not initialized
 * - Any lower-level partition/flash erase error
 */
esp_err_t flash_store_current_clear(void);

#ifdef __cplusplus
}
#endif
