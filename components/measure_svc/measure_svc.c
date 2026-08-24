/* Copyright 2026 PSU-EXT Authors */
/** @file measure_svc.c @brief Measurement-service composition root and routing. */
#include "measure_svc.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "measure_provider.h"
#include "measure_svc_average.h"
#include "measure_svc_calibration_capture.h"
#include "measure_svc_calibration_runtime.h"
#include "measure_svc_core.h"
#include "measure_svc_event_bus.h"
#include "measure_svc_history.h"
#include "measure_svc_power.h"

static const char *TAG = "measure_svc";
static const measure_provider_t *s_provider;
static bool s_initialized;
static measure_svc_config_t s_config;

static bool is_ads1115_input(measure_input_t input)
{
    return (input >= MEASURE_INPUT_ADS1115_AIN0) &&
        (input <= MEASURE_INPUT_ADS1115_AIN3);
}

static esp_err_t validate_config(const measure_svc_config_t *config)
{
    if ((config == NULL) || !is_ads1115_input(config->output_voltage_input) ||
        !is_ads1115_input(config->output_current_input) ||
        (config->output_voltage_input == config->output_current_input)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->input_voltage_input != MEASURE_INPUT_UNUSED) &&
        (!is_ads1115_input(config->input_voltage_input) ||
         (config->input_voltage_input == config->output_voltage_input) ||
         (config->input_voltage_input == config->output_current_input))) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t measure_svc_core_get_physical_input(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t *input)
{
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((channel == MEASURE_CHANNEL_0) && (kind == MEASURE_KIND_VOLTAGE)) {
        if (s_config.input_voltage_input == MEASURE_INPUT_UNUSED) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        *input = s_config.input_voltage_input;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        *input = s_config.output_voltage_input;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        *input = s_config.output_current_input;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

const measure_svc_config_t *measure_svc_core_get_config(void)
{
    return &s_config;
}

bool measure_svc_core_is_initialized(void)
{
    return s_initialized;
}

esp_err_t measure_svc_core_read_raw_sample(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4,
    int16_t *raw_code)
{
    if ((value_u4 == NULL) || (raw_code == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_provider == NULL) || (s_provider->read_raw_sample == NULL)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return s_provider->read_raw_sample(input, kind, value_u4, raw_code);
}

esp_err_t measure_svc_core_raw_code_to_u4(int16_t raw_code, uint32_t *value_u4)
{
    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_provider == NULL) || (s_provider->raw_code_to_value_u4 == NULL)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *value_u4 = s_provider->raw_code_to_value_u4(raw_code);
    return ESP_OK;
}

esp_err_t measure_svc_set_prov(const measure_provider_t *provider)
{
    if ((provider == NULL) || (provider->read_value_u4 == NULL) ||
        (provider->read_raw_value_u4 == NULL) || (provider->read_raw_sample == NULL) ||
        (provider->raw_code_to_value_u4 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_provider = provider;
    return ESP_OK;
}

const char *measure_svc_get_prov_name(void)
{
    return ((s_provider == NULL) || (s_provider->name == NULL)) ? "UNSET" : s_provider->name;
}

esp_err_t measure_svc_init_with_config(const measure_svc_config_t *config)
{
    ESP_RETURN_ON_ERROR(validate_config(config), TAG, "invalid measurement input mapping");
    if (s_initialized) {
        return (s_config.input_voltage_input == config->input_voltage_input) &&
            (s_config.output_voltage_input == config->output_voltage_input) &&
            (s_config.output_current_input == config->output_current_input)
            ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_config = *config;
    ESP_RETURN_ON_ERROR(measure_svc_event_bus_init(), TAG, "initializing event bus failed");
    ESP_RETURN_ON_ERROR(measure_svc_history_init(), TAG, "initializing history failed");
    ESP_RETURN_ON_ERROR(measure_svc_calibration_capture_init(), TAG, "initializing capture failed");
    ESP_RETURN_ON_ERROR(measure_svc_calibration_init(), TAG, "loading calibration failed");
    ESP_RETURN_ON_ERROR(measure_svc_average_init(), TAG, "loading averaging failed");
    ESP_RETURN_ON_ERROR(measure_svc_set_prov(measure_prov_ads1115_get()), TAG, "setting provider failed");
    ESP_RETURN_ON_ERROR(measure_svc_power_init(), TAG, "initializing power derivation failed");
    ESP_RETURN_ON_ERROR(measure_svc_history_register_listeners(), TAG, "registering history failed");
    ESP_RETURN_ON_ERROR(measure_svc_calibration_capture_register_listener(), TAG, "registering capture failed");

    s_initialized = true;
    ESP_LOGI(TAG, "measurement map: input voltage=%d, output voltage=AIN%d, output current=AIN%d",
        (int)s_config.input_voltage_input,
        (int)s_config.output_voltage_input,
        (int)s_config.output_current_input);
    return ESP_OK;
}

esp_err_t measure_svc_init(void)
{
    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_UNUSED,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN1,
    };
    return measure_svc_init_with_config(&config);
}
