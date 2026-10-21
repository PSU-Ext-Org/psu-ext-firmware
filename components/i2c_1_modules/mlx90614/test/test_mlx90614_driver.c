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

#include "mlx90614_module.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mlx90614.h"
#include "unity.h"

/* Only this sentinel bus/device uses injected transfers. Other tests and
 * drivers retain the real IDF behavior. The vendor read/PEC code runs intact. */
static uint8_t s_device;
static esp_err_t s_transfer_result;
static bool s_bad_pec;
static unsigned s_read_error_logs;
static vprintf_like_t s_previous_log_writer;

esp_err_t __real_i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *device);
esp_err_t __wrap_i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *device)
{
    if (bus == (i2c_master_bus_handle_t)(uintptr_t)2) {
        *device = (i2c_master_dev_handle_t)&s_device;
        return ESP_OK;
    }
    return __real_i2c_master_bus_add_device(bus, config, device);
}

esp_err_t __real_i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t __wrap_i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    return device == (i2c_master_dev_handle_t)&s_device ? ESP_OK :
        __real_i2c_master_bus_rm_device(device);
}

esp_err_t __real_i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_size, uint8_t *read_data,
    size_t read_size, int timeout);
esp_err_t __wrap_i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_size, uint8_t *read_data,
    size_t read_size, int timeout)
{
    if (device != (i2c_master_dev_handle_t)&s_device) {
        return __real_i2c_master_transmit_receive(device, write_data, write_size,
                                                read_data, read_size, timeout);
    }
    if (write_size != 1 || *write_data != 0x07 || read_size != 3 || timeout != 50) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_transfer_result == ESP_OK) {
        /* PEC 0xdd covers address-write 0xb4, command 0x07, address-read 0xb5,
         * and little-endian raw 13000 (0xc8, 0x32). */
        const uint8_t response[] = {0xc8, 0x32, s_bad_pec ? 0xdc : 0xdd};
        memcpy(read_data, response, sizeof(response));
    }
    return s_transfer_result;
}

static int count_read_error_logs(const char *format, va_list args)
{
    char message[256];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(message, sizeof(message), format, copy);
    va_end(copy);
    if (strstr(message, "read failed") != NULL) {
        ++s_read_error_logs;
    }
    return s_previous_log_writer(format, args);
}

TEST_CASE("pyrometer driver returns quiet transfer errors and retains PEC validation", "[mlx90614]")
{
    const mlx90614_config_t config = {
        .mlx90614_device = {.device_address = 0x5a, .scl_speed_hz = 100000},
    };
    mlx90614_handle_t sensor = NULL;
    TEST_ASSERT_EQUAL(ESP_OK,
        mlx90614_init((i2c_master_bus_handle_t)(uintptr_t)2, &config, &sensor));
    mlx90614_sample_t samples[1];
    mlx90614_state_t state = {
        .history = {.samples = samples},
        .history_lock = xSemaphoreCreateMutex(),
    };
    TEST_ASSERT_NOT_NULL(state.history_lock);
    const esp_err_t errors[] = {ESP_ERR_TIMEOUT, ESP_ERR_NOT_FOUND, ESP_FAIL};
    bool errors_ok = true;
    s_read_error_logs = 0;
    s_previous_log_writer = esp_log_set_vprintf(count_read_error_logs);
    for (unsigned i = 0; i < 12; ++i) {
        s_transfer_result = errors[i % 3];
        uint16_t raw = 0xffff;
        const esp_err_t err = mlx90614_read(sensor, 0x07, &raw);
        errors_ok &= err == s_transfer_result && raw == 0xffff;
        mlx90614_store_result(&state, err, raw, i);
        errors_ok &= !state.history.latest_valid && state.history.count == 0;
    }
    esp_log_set_vprintf(s_previous_log_writer);
    s_transfer_result = ESP_OK;
    s_bad_pec = true;
    uint16_t raw = 0xffff;
    const esp_err_t crc_err = mlx90614_read(sensor, 0x07, &raw);
    const bool crc_preserves_output = raw == 0xffff;
    s_bad_pec = false;
    const esp_err_t recovered = mlx90614_read(sensor, 0x07, &raw);
    mlx90614_store_result(&state, recovered, raw, 12);
    const bool sample_ok = state.history.latest_valid && state.history.count == 1 &&
        samples[0].temperature_c4 == -131500;
    vSemaphoreDelete(state.history_lock);
    const esp_err_t cleanup = mlx90614_deinit(sensor);
    TEST_ASSERT_TRUE(errors_ok);
    TEST_ASSERT_EQUAL(0, s_read_error_logs);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, crc_err);
    TEST_ASSERT_TRUE(crc_preserves_output);
    TEST_ASSERT_EQUAL(ESP_OK, recovered);
    TEST_ASSERT_EQUAL_UINT16(13000, raw);
    TEST_ASSERT_TRUE(sample_ok);
    TEST_ASSERT_EQUAL(ESP_OK, cleanup);
}
