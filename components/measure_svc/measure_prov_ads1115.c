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
 * @file measure_prov_ads1115.c
 * @brief ADS1115-backed measurement provider for SCPI channel voltage reads.
 */

#include "measure_svc.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "meas_ads1115";

static const i2c_port_t ADS_I2C_PORT = I2C_NUM_0;
static const gpio_num_t ADS_SDA_PIN = GPIO_NUM_1;
static const gpio_num_t ADS_SCL_PIN = GPIO_NUM_2;
static const uint8_t ADS_I2C_ADDR = 0x48;
static const int ADS_I2C_TIMEOUT_MS = 50;
static const TickType_t ADS_CONVERSION_POLL_DELAY = pdMS_TO_TICKS(1);
static const TickType_t ADS_CONVERSION_READY_TIMEOUT = pdMS_TO_TICKS(20);
static const float ADS_FULL_SCALE_VOLTAGE = 2.048f;

enum {
    ADS1115_REG_CONVERSION = 0x00,
    ADS1115_REG_CONFIG = 0x01,
};

typedef enum {
    ADS1115_MUX_AIN0_GND = 0x04,
    ADS1115_MUX_AIN1_GND = 0x05,
    ADS1115_MUX_AIN2_GND = 0x06,
    ADS1115_MUX_AIN3_GND = 0x07,
} ads1115_mux_t;

static bool s_ads_initialized;
static i2c_master_bus_handle_t s_ads_bus_handle;
static i2c_master_dev_handle_t s_ads_dev_handle;
static SemaphoreHandle_t s_ads_lock;
static bool s_last_mux_valid;
static ads1115_mux_t s_last_mux;

/**
 * @brief Map a physical measurement input to its ADS1115 single-ended mux input.
 *
 * @param input Physical ADS1115 input.
 * @param mux Output pointer receiving the ADS1115 mux enum.
 * @return `ESP_OK` on success, otherwise `ESP_ERR_INVALID_ARG`.
 */
static esp_err_t measure_prov_ads1115_input_to_mux(
    measure_input_t input,
    ads1115_mux_t *mux)
{
    if (mux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (input) {
    case MEASURE_INPUT_ADS1115_AIN0:
        *mux = ADS1115_MUX_AIN0_GND;
        return ESP_OK;
    case MEASURE_INPUT_ADS1115_AIN1:
        *mux = ADS1115_MUX_AIN1_GND;
        return ESP_OK;
    case MEASURE_INPUT_ADS1115_AIN2:
        *mux = ADS1115_MUX_AIN2_GND;
        return ESP_OK;
    case MEASURE_INPUT_ADS1115_AIN3:
        *mux = ADS1115_MUX_AIN3_GND;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief Assemble the ADS1115 config register for one single-ended conversion.
 *
 * The provider uses single-shot conversions because only one muxed ADS1115
 * channel can be converted at a time, while the measurement sampler alternates
 * between channels.
 *
 * The returned value programs the ADS1115 with these settings:
 * - `OS = 1`: starts a fresh conversion immediately after the config write.
 * - `MUX = mux`: selects `AIN0` through `AIN3` measured against `GND`.
 * - `PGA = +/-2.048 V`: matches the current ADC input range configuration.
 * - `MODE = 1`: single-shot mode so each sampler read selects and samples its channel.
 * - `DR = 860 SPS`: fastest data rate so both muxed channels can sustain
 *   the configured 100 Hz per-channel sampler target.
 * - `COMP_QUE = 11`: disables the comparator and leaves `ALERT/RDY` unused.
 *
 * @param mux Input multiplexer selection for the requested input.
 * @return Encoded ADS1115 config register value ready to write to `ADS1115_REG_CONFIG`.
 */
static uint16_t ads1115_build_config(ads1115_mux_t mux)
{
    /* OS=1 starts a new single conversion. */
    const uint16_t os_single_start = (uint16_t)1U << 15;
    /* MUX selects the requested single-ended input against GND. */
    const uint16_t mux_bits = ((uint16_t)mux & 0x07U) << 12;
    /* PGA=010 selects the +/-2.048 V full-scale range. */
    const uint16_t pga_2v048 = (uint16_t)0x02U << 9;
    /* MODE=1 places the ADC in single-shot mode. */
    const uint16_t single_shot_mode = (uint16_t)1U << 8;
    /* DR=111 selects 860 samples per second. */
    const uint16_t data_rate_860sps = (uint16_t)0x07U << 5;
    /* COMP_QUE=11 disables the comparator and ALERT/RDY output. */
    const uint16_t comparator_disabled = 0x0003U;

    return os_single_start | mux_bits | pga_2v048 | single_shot_mode | data_rate_860sps | comparator_disabled;
}

/**
 * @brief Write a 16-bit ADS1115 register in big-endian order.
 *
 * @param reg Register address.
 * @param value Register value to write.
 * @return `ESP_OK` on success, otherwise the I2C transaction error.
 */
static esp_err_t ads1115_write_register(uint8_t reg, uint16_t value)
{
    if (s_ads_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFU),
    };

    return i2c_master_transmit(s_ads_dev_handle, payload, sizeof(payload), ADS_I2C_TIMEOUT_MS);
}

/**
 * @brief Read a 16-bit ADS1115 register in big-endian order.
 *
 * @param reg Register address.
 * @param value Output pointer receiving the register contents.
 * @return `ESP_OK` on success, otherwise the I2C transaction error.
 */
static esp_err_t ads1115_read_register(uint8_t reg, uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ads_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(s_ads_dev_handle, &reg, sizeof(reg), raw, sizeof(raw), ADS_I2C_TIMEOUT_MS),
        TAG,
        "Failed to read ADS1115 register 0x%02X",
        reg
    );

    *value = ((uint16_t)raw[0] << 8) | raw[1];
    return ESP_OK;
}

/**
 * @brief Install the shared I2C master used by the ADS1115 provider.
 *
 * @return `ESP_OK` on success, otherwise the bus setup error.
 */
static esp_err_t ads1115_init_bus(void)
{
    if (s_ads_dev_handle != NULL) {
        return ESP_OK;
    }

    if (s_ads_bus_handle == NULL) {
        i2c_master_bus_config_t bus_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = ADS_I2C_PORT,
            .sda_io_num = ADS_SDA_PIN,
            .scl_io_num = ADS_SCL_PIN,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };

        ESP_RETURN_ON_ERROR(
            i2c_new_master_bus(&bus_config, &s_ads_bus_handle),
            TAG,
            "Failed to create ADS1115 I2C bus"
        );
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADS_I2C_ADDR,
        .scl_speed_hz = MEASURE_SVC_ADS1115_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(s_ads_bus_handle, &dev_config, &s_ads_dev_handle);
    if (err != ESP_OK) {
        esp_err_t del_err = i2c_del_master_bus(s_ads_bus_handle);
        if (del_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete ADS1115 I2C bus after device add failure: %s", esp_err_to_name(del_err));
        }
        s_ads_bus_handle = NULL;
    }

    return err;
}

/**
 * @brief Release the ADS1115 I2C resources after a failed startup probe.
 */
static void ads1115_deinit_bus(void)
{
    if (s_ads_dev_handle != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_ads_dev_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove ADS1115 I2C device: %s", esp_err_to_name(err));
        }
        s_ads_dev_handle = NULL;
    }

    if (s_ads_bus_handle != NULL) {
        esp_err_t err = i2c_del_master_bus(s_ads_bus_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete ADS1115 I2C bus: %s", esp_err_to_name(err));
        }
        s_ads_bus_handle = NULL;
    }
}

/**
 * @brief Wait until the ADS1115 reports the current conversion is complete.
 *
 * In single-shot mode the OS bit reads back as `0` while a conversion is in
 * progress and returns to `1` once the result register is ready.
 *
 * @return `ESP_OK` when conversion data is ready, otherwise a timeout or I2C error.
 */
static esp_err_t ads1115_wait_for_conversion_ready(void)
{
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < ADS_CONVERSION_READY_TIMEOUT) {
        uint16_t config = 0;
        ESP_RETURN_ON_ERROR(
            ads1115_read_register(ADS1115_REG_CONFIG, &config),
            TAG,
            "Failed to poll ADS1115 config register"
        );

        if ((config & ((uint16_t)1U << 15)) != 0U) {
            return ESP_OK;
        }

        vTaskDelay(ADS_CONVERSION_POLL_DELAY);
    }

    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Trigger one conversion on the requested mux input and return the raw code.
 *
 * @param mux ADS1115 mux selection for the channel being sampled.
 * @param raw_value Output pointer receiving the signed conversion result.
 * @return `ESP_OK` on success, otherwise the config or readback error.
 */
static esp_err_t ads1115_read_single_shot(ads1115_mux_t mux, int16_t *raw_value)
{
    if (raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t config = ads1115_build_config(mux);
    ESP_RETURN_ON_ERROR(ads1115_write_register(ADS1115_REG_CONFIG, config), TAG, "Failed to configure ADS1115");
    ESP_RETURN_ON_ERROR(
        ads1115_wait_for_conversion_ready(),
        TAG,
        "Timed out waiting for ADS1115 conversion"
    );

    uint16_t raw_register = 0;
    ESP_RETURN_ON_ERROR(
        ads1115_read_register(ADS1115_REG_CONVERSION, &raw_register),
        TAG,
        "Failed to read ADS1115 conversion register"
    );

    *raw_value = (int16_t)raw_register;
    return ESP_OK;
}

/**
 * @brief Convert a raw ADS1115 code into volts scaled by 10,000.
 *
 * @param raw_value Signed ADS1115 conversion result.
 * @return Converted reading scaled by 10,000, clamped to zero for negative values.
 */
static uint32_t ads1115_raw_to_voltage_u4(int16_t raw_value)
{
    float voltage = ((float)raw_value * ADS_FULL_SCALE_VOLTAGE) / 32767.0f;
    if (voltage <= 0.0f) {
        return 0U;
    }

    return (uint32_t)(voltage * 10000.0f);
}

/**
 * @brief Read one raw fixed-point value from the ADS1115-backed provider.
 *
 * Raw voltage reads are available from physical ADS1115 inputs `AIN0` through
 * `AIN3`. The measurement service maps configured physical inputs onto logical
 * SCPI CH1 output voltage and current readings.
 *
 * @param input Physical ADS1115 input to sample.
 * @param kind Measurement quantity to sample.
 * @param value_u4 Output pointer receiving the sampled value scaled by 10,000.
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if any argument is invalid
 * - `ESP_ERR_INVALID_STATE` if the provider was not initialized
 * - `ESP_ERR_NOT_SUPPORTED` for measurement kinds not backed by ADS1115 yet
 * - Any ADS1115 or I2C read error
 */
static esp_err_t measure_prov_ads1115_read_raw_voltage(
    measure_input_t input,
    uint32_t *value_u4)
{
    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ads_initialized || (s_ads_lock == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    ads1115_mux_t mux;
    ESP_RETURN_ON_ERROR(
        measure_prov_ads1115_input_to_mux(input, &mux),
        TAG,
        "Unsupported measurement input"
    );

    if (xSemaphoreTake(s_ads_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t raw_value = 0;
    bool mux_changed = !s_last_mux_valid || (s_last_mux != mux);
    if (mux_changed) {
        /* Throw away the first sample after a mux switch so the next one reflects the new channel cleanly. */
        esp_err_t err = ads1115_read_single_shot(mux, &raw_value);
        if (err != ESP_OK) {
            xSemaphoreGive(s_ads_lock);
            return err;
        }
    }

    esp_err_t err = ads1115_read_single_shot(mux, &raw_value);
    if (err == ESP_OK) {
        s_last_mux = mux;
        s_last_mux_valid = true;
    }
    xSemaphoreGive(s_ads_lock);
    if (err != ESP_OK) {
        return err;
    }

    *value_u4 = ads1115_raw_to_voltage_u4(raw_value);
    return ESP_OK;
}

static esp_err_t measure_prov_ads1115_read_raw(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    if (kind != MEASURE_KIND_VOLTAGE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return measure_prov_ads1115_read_raw_voltage(input, value_u4);
}

static esp_err_t measure_prov_ads1115_read(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    return measure_prov_ads1115_read_raw(input, kind, value_u4);
}

static const measure_provider_t s_ads1115_provider = {
    .name = "ADS1115",
    .read_value_u4 = measure_prov_ads1115_read,
    .read_raw_value_u4 = measure_prov_ads1115_read_raw,
};

esp_err_t measure_prov_ads1115_init(void)
{
    if (s_ads_initialized) {
        return ESP_OK;
    }

    if (s_ads_lock == NULL) {
        s_ads_lock = xSemaphoreCreateMutex();
        if (s_ads_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(ads1115_init_bus(), TAG, "Failed to initialize ADS1115 bus");

    /* One conversion confirms ADS1115 communication. Input roles are configured by measure_svc. */
    int16_t raw_value = 0;
    esp_err_t err = ads1115_read_single_shot(ADS1115_MUX_AIN0_GND, &raw_value);
    if (err != ESP_OK) {
        ads1115_deinit_bus();
        ESP_LOGE(TAG, "Failed to probe ADS1115 startup channels: %s", esp_err_to_name(err));
        return err;
    }

    s_ads_initialized = true;
    s_last_mux_valid = false;
    ESP_LOGI(
        TAG,
        "ADS1115 provider ready at 0x%02X, I2C=%" PRIu32 " Hz, single-ended AIN0..AIN3",
        ADS_I2C_ADDR,
        MEASURE_SVC_ADS1115_I2C_FREQ_HZ);
    return ESP_OK;
}

const measure_provider_t *measure_prov_ads1115_get(void)
{
    ESP_ERROR_CHECK(measure_prov_ads1115_init());
    return &s_ads1115_provider;
}
