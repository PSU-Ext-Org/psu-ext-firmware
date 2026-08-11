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
 * @file measure_svc.h
 * @brief Active measurement provider selection and readout helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "measure_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_SVC_CAL_MAX_POINTS 8U

typedef struct {
    uint32_t raw_voltage_u4;
    uint32_t actual_voltage_u4;
} measure_svc_cal_point_t;

typedef enum {
    MEASURE_SVC_CAL_TRANSACTION_IDLE = 0,
    MEASURE_SVC_CAL_TRANSACTION_OPEN,
    MEASURE_SVC_CAL_TRANSACTION_COMMITTING,
} measure_svc_cal_transaction_state_t;

typedef struct {
    measure_svc_cal_transaction_state_t state;
    measure_kind_t kind;
    measure_channel_t channel;
} measure_svc_cal_transaction_t;

typedef struct {
    uint32_t time_ms;
    uint32_t value_u4;
} measure_svc_sample_t;

typedef struct {
    measure_input_t input_voltage_input;
    measure_input_t output_voltage_input;
    measure_input_t output_current_input;
} measure_svc_config_t;

typedef struct {
    measure_kind_t kind;
    measure_channel_t channel;
    measure_input_t physical_input;
    uint32_t time_ms;
    uint32_t raw_value_u4;
    uint32_t value_u4;
} measure_svc_sample_event_t;

typedef void (*measure_svc_sample_listener_fn_t)(
    const measure_svc_sample_event_t *event,
    void *context);

#define MEASURE_SVC_ADS1115_I2C_FREQ_HZ 400000U
#define MEASURE_SVC_MIN_SAMPLE_RATE_HZ 10U
#define MEASURE_SVC_MAX_SAMPLE_RATE_HZ 100U
#define MEASURE_SVC_MIN_AVERAGE_COUNT 1U
#define MEASURE_SVC_MAX_AVERAGE_COUNT 50U
#define MEASURE_SVC_DEFAULT_AVERAGE_COUNT 10U
#define MEASURE_SVC_MAX_SAMPLE_CAPACITY 600U
#define MEASURE_SVC_MAX_SAMPLE_LISTENERS 8U

#ifndef MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ
#define MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ 10U
#endif

/**
 * @brief Initialize the measurement service with the default ADS1115 provider.
 *
 * This prepares calibration, storage listeners, and provider selection. Call
 * measure_svc_start_sampling() after all sample listeners are registered.
 *
 * @return
 * - `ESP_OK` if the service is ready
 * - `ESP_ERR_INVALID_STATE` if the ADS1115 provider is not available
 */
esp_err_t measure_svc_init(void);

/**
 * @brief Initialize the measurement service with an explicit physical input map.
 *
 * Input voltage may be `MEASURE_INPUT_UNUSED`. Output voltage and output
 * current must use distinct ADS1115 inputs. When input voltage is configured,
 * it is sampled as logical CH0 voltage.
 */
esp_err_t measure_svc_init_with_config(const measure_svc_config_t *config);

/**
 * @brief Set the shared per-channel background sample rate.
 *
 * Values outside the supported range are clamped to
 * `MEASURE_SVC_MIN_SAMPLE_RATE_HZ..MEASURE_SVC_MAX_SAMPLE_RATE_HZ`.
 * The configured rate is used when the sampler task starts.
 *
 * @param hz Requested sample rate in Hz for each active output measurement input.
 * @return `ESP_OK` on success.
 */
esp_err_t measure_svc_set_sample_rate_hz(uint32_t hz);

/**
 * @brief Start the background sampler task once.
 */
esp_err_t measure_svc_start_sampling(void);

/**
 * @brief Set the persisted scalar-query averaging count for one measurement kind.
 *
 * Values outside the supported range are clamped to
 * `MEASURE_SVC_MIN_AVERAGE_COUNT..MEASURE_SVC_MAX_AVERAGE_COUNT`.
 */
esp_err_t measure_svc_set_average_count(measure_kind_t kind, uint32_t count);

/**
 * @brief Get the active scalar-query averaging count for one measurement kind.
 */
esp_err_t measure_svc_get_average_count(measure_kind_t kind, uint32_t *count);

/**
 * @brief Register a synchronous listener for calibrated sample events.
 *
 * Listeners are called from the sampler task in registration order. The same
 * callback/context pair may be registered repeatedly for the same kind and is
 * treated as already registered.
 */
esp_err_t measure_svc_register_sample_listener(
    measure_kind_t kind,
    measure_svc_sample_listener_fn_t callback,
    void *context);

/**
 * @brief Replace the active measurement provider implementation.
 *
 * This is the seam intended for future hardware-backed implementations such as
 * an ADS1115-based reader.
 *
 * @param provider Provider descriptor to activate.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if @p provider or its read callback is invalid
 */
esp_err_t measure_svc_set_prov(const measure_provider_t *provider);

/**
 * @brief Read the requested channel/kind measurement from the active provider.
 *
 * The returned value uses fixed-point units scaled by 10,000:
 * - voltage = volts * 10000
 * - current = amps * 10000
 *
 * @param channel Measurement channel to sample.
 * @param kind Measurement quantity to sample.
 * @param value_u4 Output pointer receiving the sampled fixed-point value.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_STATE` if no active provider is configured
 * - Any provider-specific `esp_err_t` returned by the active provider
 */
esp_err_t measure_svc_read(
    measure_channel_t channel,
    measure_kind_t kind,
    uint32_t *value_u4);

/**
 * @brief Copy calibrated voltage history samples for logical CH0 or CH1.
 *
 * Samples are copied in oldest-to-newest order. Values are calibrated PSU
 * voltages scaled by 10,000.
 *
 * @param channel Logical measurement channel to export.
 * @param start_offset Oldest-relative sample offset.
 * @param max_count Maximum samples to copy.
 * @param samples Destination buffer receiving copied samples.
 * @param copied_count Output receiving number of copied records.
 *
 * @return
 * - `ESP_OK` on success, including empty selections
 * - `ESP_ERR_INVALID_ARG` for invalid arguments
 * - `ESP_ERR_INVALID_STATE` if the measurement service is not initialized
 */
esp_err_t measure_svc_copy_voltage_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count);

/**
 * @brief Copy calibrated current history samples for the logical power path.
 *
 * Samples are copied in oldest-to-newest order. Values are calibrated PSU
 * currents scaled by 10,000.
 *
 * @param channel Logical measurement channel to export. Only CH1 is public.
 * @param start_offset Oldest-relative sample offset.
 * @param max_count Maximum samples to copy.
 * @param samples Destination buffer receiving copied samples.
 * @param copied_count Output receiving number of copied records.
 *
 * @return
 * - `ESP_OK` on success, including empty selections
 * - `ESP_ERR_INVALID_ARG` for invalid arguments
 * - `ESP_ERR_INVALID_STATE` if the measurement service is not initialized
 */
esp_err_t measure_svc_copy_current_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count);

/**
 * @brief Copy derived power history samples for the logical power path.
 *
 * Samples are copied in oldest-to-newest order. Values are watts scaled by
 * 10,000 and are derived from calibrated voltage and current sample pairs.
 *
 * @param channel Logical measurement channel to export. Only CH1 is public.
 * @param start_offset Oldest-relative sample offset.
 * @param max_count Maximum samples to copy.
 * @param samples Destination buffer receiving copied samples.
 * @param copied_count Output receiving number of copied records.
 *
 * @return
 * - `ESP_OK` on success, including empty selections
 * - `ESP_ERR_INVALID_ARG` for invalid arguments
 * - `ESP_ERR_INVALID_STATE` if the measurement service is not initialized
 */
esp_err_t measure_svc_copy_power_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count);

/**
 * @brief Return the name of the currently active measurement provider.
 *
 * @return Provider name string, or `"UNSET"` if no provider is active.
 */
const char *measure_svc_get_prov_name(void);

/**
 * @brief Start a volatile calibration transaction for one measurement table.
 *
 * Supported targets are voltage CH0, voltage CH1, and current CH1. The active
 * table is copied to staging and remains in use for measurements until commit.
 */
esp_err_t measure_svc_calibration_start(
    measure_kind_t kind,
    measure_channel_t channel);

/**
 * @brief Query the device-wide calibration transaction state and target.
 *
 * The kind and channel fields are meaningful only when state is not idle.
 */
esp_err_t measure_svc_calibration_get_transaction(
    measure_svc_cal_transaction_t *transaction);

/**
 * @brief Discard the open staged calibration table.
 */
esp_err_t measure_svc_calibration_abort(void);

/**
 * @brief Capture or append one point in the matching staged table.
 *
 * Point indexes are one-based. Existing points may be replaced and exactly
 * count + 1 may be appended; indexes that create holes are rejected.
 */
esp_err_t measure_svc_calibration_capture_point(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t point_index,
    uint32_t actual_u4,
    measure_svc_cal_point_t *stored_point);

/**
 * @brief Query one staged or active point for a supported target.
 *
 * Queries for the open transaction target use staging. Other targets use their
 * active RAM tables.
 */
esp_err_t measure_svc_calibration_get_point(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t point_index,
    measure_svc_cal_point_t *point);

/**
 * @brief Clear the matching staged table so it can be rebuilt from point 1.
 */
esp_err_t measure_svc_calibration_clear(
    measure_kind_t kind,
    measure_channel_t channel);

/**
 * @brief Query the staged or active point count for a supported target.
 */
esp_err_t measure_svc_calibration_get_count(
    measure_kind_t kind,
    measure_channel_t channel,
    uint8_t *count);

/**
 * @brief Validate, persist, and publish the one open staged table.
 */
esp_err_t measure_svc_calibration_commit(void);

/**
 * @brief Compatibility wrapper for capturing logical CH0 voltage point 1 or 2.
 *
 * A matching voltage CH0 transaction must already be open. Capture changes
 * staging only; use measure_svc_calibration_commit() to persist and activate it.
 */
esp_err_t measure_svc_calibrate_input_voltage_point(
    uint8_t point_index,
    uint32_t actual_voltage_u4,
    measure_svc_cal_point_t *stored_point);

/**
 * @brief Compatibility wrapper for capturing logical CH1 voltage point 1 or 2.
 *
 * The function waits for the next fresh configured output-voltage sample and
 * pairs that raw voltage with @p actual_voltage_u4.
 *
 * @param point_index Calibration point index, either 1 or 2.
 * @param actual_voltage_u4 Real PSU voltage scaled by 10,000.
 * @param stored_point Optional output receiving the raw/actual pair saved.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for an invalid point index
 * - `ESP_ERR_INVALID_STATE` if a matching transaction is not open
 * - `ESP_ERR_NOT_SUPPORTED` if the active provider cannot provide raw readings
 * - `ESP_ERR_TIMEOUT` if no fresh voltage sample arrives before the calibration timeout
 * - An ESP-IDF error code if sampling fails
 */
esp_err_t measure_svc_calibrate_ch0_point(
    uint8_t point_index,
    uint32_t actual_voltage_u4,
    measure_svc_cal_point_t *stored_point);

/**
 * @brief Compatibility wrapper for capturing logical CH1 current point 1 or 2.
 *
 * The function waits for the next fresh configured output-current sample and
 * pairs that raw current-sense amplifier voltage with @p actual_current_u4.
 *
 * @param point_index Calibration point index, either 1 or 2.
 * @param actual_current_u4 Real PSU current scaled by 10,000.
 * @param stored_point Optional output receiving the raw/actual pair saved.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for an invalid point index
 * - `ESP_ERR_INVALID_STATE` if a matching transaction is not open
 * - `ESP_ERR_NOT_SUPPORTED` if the active provider cannot provide raw readings
 * - `ESP_ERR_TIMEOUT` if no fresh current-sense sample arrives before the calibration timeout
 * - An ESP-IDF error code if sampling fails
 */
esp_err_t measure_svc_calibrate_current_point(
    uint8_t point_index,
    uint32_t actual_current_u4,
    measure_svc_cal_point_t *stored_point);

/**
 * @brief Return one active logical CH0 input-voltage calibration point.
 */
esp_err_t measure_svc_get_input_voltage_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point);

/**
 * @brief Return one active logical CH1 voltage calibration point.
 *
 * @param point_index Calibration point index, either 1 or 2.
 * @param point Output receiving the raw/actual pair.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for invalid arguments
 */
esp_err_t measure_svc_get_ch0_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point);

/**
 * @brief Return one active logical CH1 current calibration point.
 *
 * @param point_index Calibration point index, either 1 or 2.
 * @param point Output receiving the raw/actual pair.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for invalid arguments
 */
esp_err_t measure_svc_get_current_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point);

/**
 * @brief Return both active logical CH0 input-voltage calibration points.
 */
void measure_svc_get_input_voltage_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2);

/**
 * @brief Return both active logical CH1 voltage calibration points.
 *
 * @param point1 Optional output receiving calibration point 1.
 * @param point2 Optional output receiving calibration point 2.
 */
void measure_svc_get_ch0_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2);

/**
 * @brief Return both active logical CH1 current calibration points.
 *
 * @param point1 Optional output receiving calibration point 1.
 * @param point2 Optional output receiving calibration point 2.
 */
void measure_svc_get_current_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2);

/**
 * @brief Apply logical CH0 input-voltage two-point calibration to an ADS1115 input voltage.
 */
uint32_t measure_svc_apply_input_voltage_calibration_u4(uint32_t adc_voltage_u4);

/**
 * @brief Apply logical CH1 voltage two-point calibration to an ADS1115 input voltage.
 *
 * @param adc_voltage_u4 ADC input voltage scaled by 10,000.
 * @return Corrected PSU-side voltage scaled by 10,000.
 */
uint32_t measure_svc_apply_ch0_calibration_u4(uint32_t adc_voltage_u4);

/**
 * @brief Apply logical CH1 current two-point calibration to an ADS1115 input voltage.
 *
 * @param adc_voltage_u4 Current-sense amplifier output voltage scaled by 10,000.
 * @return Corrected PSU current scaled by 10,000.
 */
uint32_t measure_svc_apply_current_calibration_u4(uint32_t adc_voltage_u4);

/**
 * @brief Initialize the built-in ADS1115 measurement provider.
 *
 * @return
 * - `ESP_OK` on success
 * - Any ADS1115 startup or I2C initialization error
 */
esp_err_t measure_prov_ads1115_init(void);

/**
 * @brief Return the built-in ADS1115 measurement provider descriptor.
 *
 * @return Pointer to the static ADS1115 provider descriptor.
 */
const measure_provider_t *measure_prov_ads1115_get(void);

/**
 * @brief Return the built-in fake/random measurement provider descriptor.
 *
 * @return Pointer to the static fake provider descriptor.
 */
const measure_provider_t *measure_prov_fake_get(void);

#ifdef __cplusplus
}
#endif
