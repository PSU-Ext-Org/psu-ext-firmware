/*
MIT License

Copyright (c) 2024 Larry Li

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "mlx90614.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char TAG[] = "mlx90614";

struct mlx90614_t {
    i2c_master_dev_handle_t i2c_dev; /*!< I2C device handle */
    uint8_t device_address;          /*!< PEC calculate */
};

static uint8_t calculate_pec(uint8_t init_pec, uint8_t new_data)
{
    uint8_t data;
    uint8_t bitCheck;

    data = init_pec ^ new_data;

    for (int i = 0; i < 8; i++) {
        bitCheck = data & 0x80;
        data = data << 1;

        if (bitCheck != 0) {
            data = data ^ 0x07;
        }
    }
    return data;
}

esp_err_t mlx90614_init(i2c_master_bus_handle_t bus_handle,
                        const mlx90614_config_t *mlx90614_config,
                        mlx90614_handle_t *mlx90614_handle)
{
    ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid i2c master bus");
    ESP_RETURN_ON_FALSE(mlx90614_config, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 config");
    esp_err_t ret = ESP_OK;
    mlx90614_handle_t out_handle =
        (mlx90614_handle_t)calloc(1, sizeof(struct mlx90614_t));
    ESP_GOTO_ON_FALSE(out_handle, ESP_ERR_NO_MEM, err, TAG,
                      "no memory for i2c mlx90614 device");

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = mlx90614_config->mlx90614_device.scl_speed_hz,
        .device_address = mlx90614_config->mlx90614_device.device_address,
    };
    out_handle->device_address = i2c_dev_conf.device_address << 1;
    if (out_handle->i2c_dev == NULL) {
        ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(bus_handle, &i2c_dev_conf,
                                                    &out_handle->i2c_dev),
                          err, TAG, "i2c new bus failed");
    }

    *mlx90614_handle = out_handle;

    return ESP_OK;

err:
    if (out_handle && out_handle->i2c_dev) {
        i2c_master_bus_rm_device(out_handle->i2c_dev);
    }
    free(out_handle);
    return ret;
}

esp_err_t mlx90614_deinit(mlx90614_handle_t mlx90614_handle)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(mlx90614_handle->i2c_dev), TAG,
                        "rm i2c device failed");
    free(mlx90614_handle);
    return ESP_OK;
}

esp_err_t mlx90614_write(mlx90614_handle_t mlx90614_handle, uint8_t command,
                         const uint16_t data)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint8_t buf[4];

    buf[0] = command;
    buf[1] = data & 0xff;
    buf[2] = (data >> 8) & 0xff;
    buf[3] = calculate_pec(0, mlx90614_handle->device_address);
    buf[3] = calculate_pec(buf[3], buf[0]);
    buf[3] = calculate_pec(buf[3], buf[1]);
    buf[3] = calculate_pec(buf[3], buf[2]);

    return i2c_master_transmit(mlx90614_handle->i2c_dev, buf, sizeof(buf), 50);
}

esp_err_t mlx90614_read(mlx90614_handle_t mlx90614_handle, uint8_t command,
                        uint16_t *data)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint8_t buf[3];

    const esp_err_t err = i2c_master_transmit_receive(mlx90614_handle->i2c_dev,
                                                    &command, sizeof(command),
                                                    buf, sizeof(buf), 50);
    /* The polling module reports failure/recovery transitions. */
    if (err != ESP_OK) {
        return err;
    }

    uint8_t pec;
    pec = calculate_pec(0, mlx90614_handle->device_address);
    pec = calculate_pec(pec, command);
    pec = calculate_pec(pec, mlx90614_handle->device_address | 1);
    pec = calculate_pec(pec, buf[0]);
    pec = calculate_pec(pec, buf[1]);
    if (pec != buf[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    *data = (buf[1] << 8) | buf[0];
    return ESP_OK;
}

esp_err_t mlx90614_command(mlx90614_handle_t mlx90614_handle, uint8_t command)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    if (command != 0x60 && command != 0x61 && command != 0xff) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[2];

    buf[0] = command;
    buf[1] = calculate_pec(0, mlx90614_handle->device_address);
    buf[1] = calculate_pec(buf[1], buf[0]);

    return i2c_master_transmit(mlx90614_handle->i2c_dev, buf, sizeof(buf), 50);
}

esp_err_t mlx90614_dump_eeprom(mlx90614_handle_t mlx90614_handle,
                               uint16_t *eeprom, size_t size)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t *p = eeprom;

    for (size_t i = 0; i < size; i++, p++) {
        ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x20 | i, p), TAG,
                            "dump eeprom address 0x%02x failed", i);
    }
    return ESP_OK;
}

esp_err_t mlx90614_get_ta(mlx90614_handle_t mlx90614_handle, float *ta)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "no mem for buffer");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x06, &data), TAG,
                        "get ta failed");
    if (data > 0x7FFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *ta = (float)data * 0.02 - 273.15;
    return ESP_OK;
}

esp_err_t mlx90614_get_to(mlx90614_handle_t mlx90614_handle, float *to)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x07, &data), TAG,
                        "get to failed");
    if (data > 0x7FFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *to = (float)data * 0.02 - 273.15;
    return ESP_OK;
}

esp_err_t mlx90614_get_to2(mlx90614_handle_t mlx90614_handle, float *to2)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x08, &data), TAG,
                        "get to2 failed");
    if (data > 0x7FFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *to2 = (float)data * 0.02 - 273.15;
    return ESP_OK;
}

esp_err_t mlx90614_get_ir_data1(mlx90614_handle_t mlx90614_handle,
                                uint16_t *ir1)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x04, ir1), TAG,
                        "get ir data1 failed");
    return ESP_OK;
}

esp_err_t mlx90614_get_ir_data2(mlx90614_handle_t mlx90614_handle,
                                uint16_t *ir2)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x05, ir2), TAG,
                        "get ir data2 failed");
    return ESP_OK;
}

esp_err_t mlx90614_get_emissivity(mlx90614_handle_t mlx90614_handle,
                                  float *emissivity)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x24, &data), TAG,
                        "get emissivity failed");
    *emissivity = (float)data / 0xFFFF;
    return ESP_OK;
}

esp_err_t mlx90614_set_emissivity(mlx90614_handle_t mlx90614_handle,
                                  float value)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data;
    uint16_t curE;
    uint16_t newE = 0;
    float temp = 0;

    if (value > 1.0 || value < 0.05) {
        return ESP_ERR_INVALID_ARG;
    }
    temp = value * 65535 + 0.5;
    newE = temp;
    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x24, &curE), TAG,
                        "get emissivity failed");
    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x2F, &data), TAG,
                        "get mlx data failed");
    temp = curE * data;
    temp = temp / newE + 0.5;
    data = temp;
    if (data > 0x7FFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_RETURN_ON_ERROR(mlx90614_command(mlx90614_handle, 0x60), TAG,
                        "send command failed");
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x24, 0x0000), TAG,
                        "clean emissivity failed");
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x24, newE), TAG,
                        "set emissivity failed");
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x2F, 0x0000), TAG,
                        "clean mlx data failed");
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x2F, data), TAG,
                        "set mlx data failed");
    ESP_RETURN_ON_ERROR(mlx90614_command(mlx90614_handle, 0x61), TAG,
                        "send command failed");
    return ESP_OK;
}

esp_err_t mlx90614_get_fir(mlx90614_handle_t mlx90614_handle, uint8_t *fir)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x25, &data), TAG,
                        "get config register failed");
    data = data >> 8;
    data = data & 0x0007;
    *fir = data;
    return ESP_OK;
}

esp_err_t mlx90614_set_fir(mlx90614_handle_t mlx90614_handle, uint8_t value)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;
    uint16_t val = value & 0x0007;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x25, &data), TAG,
                        "get config register failed");
    if (val > 0x0003) {
        val = val << 8;
        data = data & 0xF8FF;
        data = data + val;
        ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x25, 0x0000), TAG,
                            "clean config register failed");
        ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x25, data), TAG,
                            "set config register failed");
    }
    return ESP_OK;
}

esp_err_t mlx90614_get_iir(mlx90614_handle_t mlx90614_handle, uint8_t *iir)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x25, &data), TAG,
                        "get config register failed");
    data = data & 0x0007;
    *iir = data;
    return ESP_OK;
}

esp_err_t mlx90614_set_iir(mlx90614_handle_t mlx90614_handle, uint8_t value)
{
    ESP_RETURN_ON_FALSE(mlx90614_handle, ESP_ERR_INVALID_ARG, TAG,
                        "invalid mlx90614 handle");
    uint16_t data = 0;
    uint8_t val = value & 0x0007;

    ESP_RETURN_ON_ERROR(mlx90614_read(mlx90614_handle, 0x25, &data), TAG,
                        "get config register failed");
    data = data & 0xFFF8;
    data = data + val;
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x25, 0x0000), TAG,
                        "clean config register failed");
    ESP_RETURN_ON_ERROR(mlx90614_write(mlx90614_handle, 0x25, data), TAG,
                        "set config register failed");
    return ESP_OK;
}

float mlx90614_temperature_in_fahrenheit(float temperature)
{
    return temperature * 1.8f + 32.0;
}

int16_t mlx90614_convert_ir_data(uint16_t ir)
{
    int16_t ir2c = ir;

    if (ir > 0x7FFF) {
        ir2c = 0x8000 - ir2c;
    }
    return ir2c;
}
