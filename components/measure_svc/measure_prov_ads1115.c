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
#include "measure_provider.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define ADS_ACQUISITION_TASK_STACK_WORDS 3072U
#define ADS_ACQUISITION_TASK_PRIORITY 6U
#define ADS_SCANNED_INPUT_COUNT 3U
#define ADS_CACHE_ENTRY_COUNT 4U
#define ADS_DATA_RATE_TOLERANCE_DENOMINATOR 9U
#define ADS1115_DEFAULT_DATA_RATE_SPS 128U
#define ADS_NVS_NAMESPACE "meas_cfg"
#define ADS_NVS_DATA_RATE_KEY "ads_rate_sps"

static const char *TAG = "meas_ads1115";

static const i2c_port_t ADS_I2C_PORT = I2C_NUM_0;
static const gpio_num_t ADS_SDA_PIN = GPIO_NUM_1;
static const gpio_num_t ADS_SCL_PIN = GPIO_NUM_2;
static const uint8_t ADS_I2C_ADDR = 0x48;
static const int ADS_I2C_TIMEOUT_MS = 50;

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

typedef struct {
    bool valid;
    uint32_t value_u4;
    int16_t raw_code;
    uint32_t source_generation;
} ads1115_cached_sample_t;

static const measure_input_t ADS_SCANNED_INPUTS[ADS_SCANNED_INPUT_COUNT] = {
    MEASURE_INPUT_ADS1115_AIN0,
    MEASURE_INPUT_ADS1115_AIN1,
    MEASURE_INPUT_ADS1115_AIN2,
};

static bool s_ads_initialized;
static i2c_master_bus_handle_t s_ads_bus_handle;
static i2c_master_dev_handle_t s_ads_dev_handle;
static SemaphoreHandle_t s_cache_lock;
static TaskHandle_t s_acquisition_task_handle;
static ads1115_cached_sample_t *s_sample_cache;
static uint16_t s_data_rate_sps = ADS1115_DEFAULT_DATA_RATE_SPS;
static uint32_t s_rate_epoch;
static uint32_t s_source_generation;

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

/** @brief Encode one supported ADS1115 conversion rate. */
static esp_err_t ads1115_data_rate_bits(uint16_t sps, uint16_t *bits)
{
    if (bits == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (sps) {
    case 8U:
        *bits = (uint16_t)0x00U << 5;
        return ESP_OK;
    case 16U:
        *bits = (uint16_t)0x01U << 5;
        return ESP_OK;
    case 32U:
        *bits = (uint16_t)0x02U << 5;
        return ESP_OK;
    case 64U:
        *bits = (uint16_t)0x03U << 5;
        return ESP_OK;
    case 128U:
        *bits = (uint16_t)0x04U << 5;
        return ESP_OK;
    case 250U:
        *bits = (uint16_t)0x05U << 5;
        return ESP_OK;
    case 475U:
        *bits = (uint16_t)0x06U << 5;
        return ESP_OK;
    case 860U:
        *bits = (uint16_t)0x07U << 5;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

/** @brief Return a tick-rounded conversion delay including -10% rate tolerance. */
static TickType_t ads1115_conversion_delay_ticks(uint16_t sps)
{
    const uint32_t delay_ms =
        (10000U + (ADS_DATA_RATE_TOLERANCE_DENOMINATOR * sps) - 1U) /
        (ADS_DATA_RATE_TOLERANCE_DENOMINATOR * sps);
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    if ((ticks == 0U) || ((ticks * portTICK_PERIOD_MS) < delay_ms)) {
        ++ticks;
    }
    return ticks;
}

/**
 * @brief Assemble the ADS1115 config register for one single-shot conversion.
 *
 * The returned value programs the ADS1115 with these settings:
 * - `MUX = mux`: selects `AIN0` through `AIN3` measured against `GND`.
 * - `PGA = +/-2.048 V`: matches the current ADC input range configuration.
 * - `OS = 1`: starts a conversion.
 * - `MODE = 1`: single-shot mode.
 * - `DR`: selected by the persistent runtime data-rate setting.
 * - `COMP_QUE = 11`: disables the comparator and leaves `ALERT/RDY` unused.
 *
 * @param mux Input multiplexer selection for the requested input.
 * @return Encoded ADS1115 config register value ready to write to `ADS1115_REG_CONFIG`.
 */
static uint16_t ads1115_build_config(ads1115_mux_t mux, uint16_t data_rate)
{
    /* OS=1 starts a conversion while the ADC is in power-down state. */
    const uint16_t start_conversion = (uint16_t)1U << 15;
    /* MUX selects the requested single-ended input against GND. */
    const uint16_t mux_bits = ((uint16_t)mux & 0x07U) << 12;
    /* PGA=010 selects the +/-2.048 V full-scale range. */
    const uint16_t pga_2v048 = (uint16_t)0x02U << 9;
    /* MODE=1 selects single-shot conversion. */
    const uint16_t single_shot_mode = (uint16_t)1U << 8;
    uint16_t data_rate_bits = 0U;
    (void)ads1115_data_rate_bits(data_rate, &data_rate_bits);
    /* COMP_QUE=11 disables the comparator and ALERT/RDY output. */
    const uint16_t comparator_disabled = 0x0003U;

    return start_conversion | mux_bits | pga_2v048 | single_shot_mode |
        data_rate_bits | comparator_disabled;
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

static esp_err_t ads1115_load_data_rate(void)
{
    nvs_handle_t handle;
    uint16_t stored_sps = ADS1115_DEFAULT_DATA_RATE_SPS;
    esp_err_t err = nvs_open(ADS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_data_rate_sps = ADS1115_DEFAULT_DATA_RATE_SPS;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "opening ADC rate settings failed");
    err = nvs_get_u16(handle, ADS_NVS_DATA_RATE_KEY, &stored_sps);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_data_rate_sps = ADS1115_DEFAULT_DATA_RATE_SPS;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "reading ADC rate setting failed");

    uint16_t bits;
    if (ads1115_data_rate_bits(stored_sps, &bits) != ESP_OK) {
        ESP_LOGW(TAG, "Ignoring unsupported stored ADC rate %u SPS", (unsigned)stored_sps);
        stored_sps = ADS1115_DEFAULT_DATA_RATE_SPS;
    }
    s_data_rate_sps = stored_sps;
    return ESP_OK;
}

static esp_err_t ads1115_store_data_rate(uint16_t sps)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(
        nvs_open(ADS_NVS_NAMESPACE, NVS_READWRITE, &handle),
        TAG,
        "opening ADC rate settings failed");
    esp_err_t err = nvs_set_u16(handle, ADS_NVS_DATA_RATE_KEY, sps);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief Trigger and read one single-shot conversion.
 *
 * @param mux ADS1115 mux selection for the channel being sampled.
 * @param raw_value Output pointer receiving the signed conversion result.
 * @return `ESP_OK` on success, otherwise the configuration or read error.
 */
static esp_err_t ads1115_read_single_shot(
    ads1115_mux_t mux,
    uint16_t data_rate_sps,
    int16_t *raw_value)
{
    if (raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t config = ads1115_build_config(mux, data_rate_sps);
    ESP_RETURN_ON_ERROR(
        ads1115_write_register(ADS1115_REG_CONFIG, config),
        TAG,
        "Failed to start ADS1115 conversion"
    );
    vTaskDelay(ads1115_conversion_delay_ticks(data_rate_sps));
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
    if (raw_value <= 0) {
        return 0U;
    }

    return (uint32_t)(((uint64_t)(uint16_t)raw_value * 20480U + 16383U) / 32767U);
}

/** @brief Scan AIN0..AIN2 with single-shot conversions and refresh their caches. */
static void ads1115_acquisition_task(void *arg)
{
    (void)arg;

    while (true) {
        for (size_t index = 0; index < ADS_SCANNED_INPUT_COUNT; ++index) {
            const measure_input_t input = ADS_SCANNED_INPUTS[index];
            ads1115_mux_t mux;
            esp_err_t err = measure_prov_ads1115_input_to_mux(input, &mux);
            if (err == ESP_OK) {
                uint16_t data_rate_sps;
                uint32_t rate_epoch;
                if (xSemaphoreTake(s_cache_lock, portMAX_DELAY) != pdTRUE) {
                    continue;
                }
                data_rate_sps = s_data_rate_sps;
                rate_epoch = s_rate_epoch;
                xSemaphoreGive(s_cache_lock);

                int16_t raw_code = 0;
                err = ads1115_read_single_shot(mux, data_rate_sps, &raw_code);
                if (err == ESP_OK) {
                    if (xSemaphoreTake(s_cache_lock, portMAX_DELAY) != pdTRUE) {
                        continue;
                    }
                    if (rate_epoch == s_rate_epoch) {
                        ++s_source_generation;
                        if (s_source_generation == 0U) {
                            ++s_source_generation;
                        }
                        s_sample_cache[(size_t)input] = (ads1115_cached_sample_t){
                            .valid = true,
                            .value_u4 = ads1115_raw_to_voltage_u4(raw_code),
                            .raw_code = raw_code,
                            .source_generation = s_source_generation,
                        };
                    }
                    xSemaphoreGive(s_cache_lock);
                }
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to acquire AIN%d: %s", (int)input, esp_err_to_name(err));
            }
        }
    }
}

static esp_err_t measure_prov_ads1115_read_raw_sample(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4,
    int16_t *raw_code,
    uint32_t *source_generation)
{
    if ((value_u4 == NULL) || (raw_code == NULL) || (source_generation == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kind != MEASURE_KIND_VOLTAGE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!s_ads_initialized || (s_cache_lock == NULL) || (s_sample_cache == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    ads1115_mux_t mux;
    ESP_RETURN_ON_ERROR(measure_prov_ads1115_input_to_mux(input, &mux), TAG, "Unsupported measurement input");
    (void)mux;
    if ((input < MEASURE_INPUT_ADS1115_AIN0) || (input > MEASURE_INPUT_ADS1115_AIN2)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(s_cache_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const ads1115_cached_sample_t cached = s_sample_cache[(size_t)input];
    xSemaphoreGive(s_cache_lock);
    if (!cached.valid) {
        return ESP_ERR_NOT_FINISHED;
    }

    *value_u4 = cached.value_u4;
    *raw_code = cached.raw_code;
    *source_generation = cached.source_generation;
    return ESP_OK;
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

    int16_t raw_value = 0;
    uint32_t source_generation = 0U;
    return measure_prov_ads1115_read_raw_sample(
        input, MEASURE_KIND_VOLTAGE, value_u4, &raw_value, &source_generation);
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
    .read_raw_sample = measure_prov_ads1115_read_raw_sample,
    .raw_code_to_value_u4 = ads1115_raw_to_voltage_u4,
};

esp_err_t measure_prov_ads1115_set_data_rate_sps(uint16_t sps)
{
    uint16_t bits;
    ESP_RETURN_ON_ERROR(ads1115_data_rate_bits(sps, &bits), TAG, "unsupported ADC rate");
    (void)bits;
    if (!s_ads_initialized || (s_cache_lock == NULL) || (s_sample_cache == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ads1115_store_data_rate(sps), TAG, "persisting ADC rate failed");
    if (xSemaphoreTake(s_cache_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_data_rate_sps = sps;
    ++s_rate_epoch;
    memset(s_sample_cache, 0, ADS_CACHE_ENTRY_COUNT * sizeof(*s_sample_cache));
    xSemaphoreGive(s_cache_lock);
    ESP_LOGI(TAG, "ADS1115 data rate changed to %u SPS", (unsigned)sps);
    return ESP_OK;
}

esp_err_t measure_prov_ads1115_get_data_rate_sps(uint16_t *sps)
{
    if (sps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ads_initialized || (s_cache_lock == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_cache_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *sps = s_data_rate_sps;
    xSemaphoreGive(s_cache_lock);
    return ESP_OK;
}

esp_err_t measure_prov_ads1115_init(void)
{
    if (s_ads_initialized) {
        return ESP_OK;
    }

    if (s_cache_lock == NULL) {
        s_cache_lock = xSemaphoreCreateMutex();
        if (s_cache_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_sample_cache == NULL) {
        s_sample_cache = heap_caps_calloc(
            ADS_CACHE_ENTRY_COUNT,
            sizeof(*s_sample_cache),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_sample_cache == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = ads1115_load_data_rate();
    if (err != ESP_OK) {
        heap_caps_free(s_sample_cache);
        s_sample_cache = NULL;
        return err;
    }

    err = ads1115_init_bus();
    if (err != ESP_OK) {
        heap_caps_free(s_sample_cache);
        s_sample_cache = NULL;
        return err;
    }

    /* Reading the config register confirms ADS1115 communication. */
    uint16_t config = 0;
    err = ads1115_read_register(ADS1115_REG_CONFIG, &config);
    if (err != ESP_OK) {
        ads1115_deinit_bus();
        heap_caps_free(s_sample_cache);
        s_sample_cache = NULL;
        ESP_LOGE(TAG, "Failed to read ADS1115 config register: %s", esp_err_to_name(err));
        return err;
    }

    s_ads_initialized = true;
    if (xTaskCreate(
            ads1115_acquisition_task,
            "ads1115_acquire",
            ADS_ACQUISITION_TASK_STACK_WORDS,
            NULL,
            ADS_ACQUISITION_TASK_PRIORITY,
            &s_acquisition_task_handle) != pdPASS) {
        s_ads_initialized = false;
        ads1115_deinit_bus();
        heap_caps_free(s_sample_cache);
        s_sample_cache = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        TAG,
        "ADS1115 provider ready at 0x%02X, I2C=%" PRIu32
        " Hz, single-shot %u SPS, cached AIN0..AIN2",
        ADS_I2C_ADDR,
        MEASURE_SVC_ADS1115_I2C_FREQ_HZ,
        (unsigned)s_data_rate_sps);
    return ESP_OK;
}

const measure_provider_t *measure_prov_ads1115_get(void)
{
    return &s_ads1115_provider;
}
