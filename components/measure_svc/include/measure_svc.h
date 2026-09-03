/* Copyright 2026 PSU-EXT Authors */
/**
 * @file measure_svc.h
 * @brief Measurement-service configuration, lifecycle, and diagnostics.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_SVC_ADS1115_I2C_FREQ_HZ 400000U /**< ADS1115 bus rate. */
#define MEASURE_SVC_MIN_SAMPLE_RATE_HZ 10U /**< Minimum rate per active input. */
#define MEASURE_SVC_MAX_SAMPLE_RATE_HZ 100U /**< Maximum rate per active input. */

#ifndef MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ
/** @brief Default rate per active input when the application does not override it. */
#define MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ 100U
#endif

/** @brief Mapping from logical measurements to physical provider inputs. */
typedef struct {
    measure_input_t input_voltage_input;  /**< CH0 voltage input, or unused. */
    measure_input_t output_voltage_input; /**< CH1 output-voltage input. */
    measure_input_t output_current_input; /**< CH1 output-current sense input. */
} measure_svc_config_t;

/** @brief Initialize with the default CH1 mapping and no CH0 input. */
esp_err_t measure_svc_init(void);
/**
 * @brief Initialize all measurement-service modules with an explicit mapping.
 * @param config Valid, non-overlapping physical input mapping.
 * @return `ESP_OK` or an ESP-IDF error from validation or module setup.
 */
esp_err_t measure_svc_init_with_config(const measure_svc_config_t *config);
/** @brief Set the sampling rate per active physical input, clamped to limits. */
esp_err_t measure_svc_set_sample_rate_hz(uint32_t hz);
/** @brief Persist and activate an ADS1115 rate: 8, 16, 32, 64, 128, 250, 475, or 860 SPS. */
esp_err_t measure_svc_set_adc_data_rate_sps(uint16_t sps);
/** @brief Read the active ADS1115 conversion rate. */
esp_err_t measure_svc_get_adc_data_rate_sps(uint16_t *sps);
/** @brief Start the background sampler; repeated calls are harmless. */
esp_err_t measure_svc_start_sampling(void);
/** @brief Return the active provider name, or a diagnostic fallback string. */
const char *measure_svc_get_prov_name(void);

#ifdef __cplusplus
}
#endif
