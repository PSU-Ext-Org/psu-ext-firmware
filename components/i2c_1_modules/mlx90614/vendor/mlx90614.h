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

#ifndef PSU_EXT_I2C_1_MODULES_MLX90614_VENDOR_MLX90614_H_
#define PSU_EXT_I2C_1_MODULES_MLX90614_VENDOR_MLX90614_H_

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#define MLX90614_DEFAULT_ADDRESS (0x5A)
#define MLX90614_EEPROM_SIZE (0x20)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_device_config_t
        mlx90614_device; /*!< Configuration for mlx90614 device */
} mlx90614_config_t;

typedef struct mlx90614_t *mlx90614_handle_t;

/**
 * @brief Init an MLX90614 device.
 *
 * @param[in] bus_handle I2C master bus handle
 * @param[in] mlx90614_config Configuration of MLX90614
 * @param[out] mlx90614_handle Handle of MLX90614
 * @return ESP_OK: Init success. ESP_FAIL: Not success.
 */
esp_err_t mlx90614_init(i2c_master_bus_handle_t bus_handle,
                        const mlx90614_config_t *mlx90614_config,
                        mlx90614_handle_t *mlx90614_handle);

/**
 * @brief Deinitialize and free MLX90614 handle.
 *
 * This will remove the device from the I2C bus and free any allocated
 * resources associated with the handle.
 *
 * @param[in] mlx90614_handle Handle returned by mlx90614_init
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_deinit(mlx90614_handle_t mlx90614_handle);

/**
 * @brief Write data to MLX90614
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] command MLX90614 command
 * @param[in] data Data to write
 * @return ESP_OK: Write success. Otherwise failed, please check I2C function
 * fail reason.
 */
esp_err_t mlx90614_write(mlx90614_handle_t mlx90614_handle, uint8_t command,
                         const uint16_t data);

/**
 * @brief Read a 16-bit value from the device using a register/command.
 *
 * The function performs an I2C read of two data bytes and a PEC (CRC).
 * The returned 16-bit value is little-endian (low byte first).
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] command Register/command to read from
 * @param[out] data Pointer to a uint16_t to receive the read value
 * @return ESP_OK on success. ESP_ERR_INVALID_CRC if PEC mismatch. Other
 *         ESP error codes on I2C or parameter errors.
 */
esp_err_t mlx90614_read(mlx90614_handle_t mlx90614_handle, uint8_t command,
                        uint16_t *data);

/**
 * @brief Send a simple command to the MLX90614 (no data payload).
 *
 * Certain device operations require sending a single-byte command such as
 * initiating EEPROM write cycles. Valid command values depend on device
 * specification (example: 0x60, 0x61 for EEPROM unlock/lock).
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] command Command byte to send
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_command(mlx90614_handle_t mlx90614_handle, uint8_t command);

/**
 * @brief Read a block of EEPROM registers from the sensor.
 *
 * Reads `size` words from EEPROM (starting at address 0) into the provided
 * buffer. Each EEPROM entry is a 16-bit value.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] eeprom Pointer to buffer to receive EEPROM words
 * @param[in] size Number of 16-bit words to read (buffer must be at least
 *            `size * sizeof(uint16_t)` bytes)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_dump_eeprom(mlx90614_handle_t mlx90614_handle,
                               uint16_t *eeprom, size_t size);

/**
 * @brief Get the ambient (object) temperature TA in degrees Celsius.
 *
 * Reads the ambient temperature register and converts the raw 16-bit value
 * into degrees Celsius using the sensor scale (value * 0.02 - 273.15).
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] ta Pointer to a float to receive the ambient temperature (°C)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_ta(mlx90614_handle_t mlx90614_handle, float *ta);

/**
 * @brief Get the object temperature TO from thermopile 1 in degrees Celsius.
 *
 * Reads the object temperature register and applies the sensor conversion
 * (value * 0.02 - 273.15).
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] to Pointer to a float to receive the object temperature (°C)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_to(mlx90614_handle_t mlx90614_handle, float *to);

/**
 * @brief Get the object temperature TO2 from thermopile 2 in degrees Celsius.
 *
 * Some MLX90614 variants provide a secondary object channel. This returns
 * the corresponding temperature using the same scale as other temperature
 * getters.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] to2 Pointer to a float to receive the second object temp (°C)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_to2(mlx90614_handle_t mlx90614_handle, float *to2);

/**
 * @brief Read raw IR thermopile data (IR1) as a 16-bit value.
 *
 * The raw IR value can be converted to a signed value using
 * mlx90614_convert_ir_data() if required.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] ir1 Pointer to receive the raw 16-bit IR data
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_ir_data1(mlx90614_handle_t mlx90614_handle,
                                uint16_t *ir1);

/**
 * @brief Read raw IR thermopile data (IR2) as a 16-bit value.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] ir2 Pointer to receive the raw 16-bit IR data
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_ir_data2(mlx90614_handle_t mlx90614_handle,
                                uint16_t *ir2);

/**
 * @brief Retrieve the current emissivity value stored in EEPROM.
 *
 * Emissivity is returned as a floating point value in the range (0, 1].
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] emissivity Pointer to float to receive emissivity
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_emissivity(mlx90614_handle_t mlx90614_handle,
                                  float *emissivity);

/**
 * @brief Set the emissivity value stored in the sensor EEPROM.
 *
 * The function validates the provided emissivity (allowed range typically
 * 0.05 - 1.0), computes the proper EEPROM value and writes it along with
 * any related compensation values required by the device.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] value Emissivity value (0.05 .. 1.0)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid input, or other
 *         ESP error codes for I2C/EERPOM failures.
 */
esp_err_t mlx90614_set_emissivity(mlx90614_handle_t mlx90614_handle,
                                  float value);

/**
 * @brief Get the FIR (filter) configuration value from the config register.
 *
 * The FIR setting affects the device's internal filtering of measured
 * values. The exact meaning of returned values is defined in the device
 * datasheet.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] fir Pointer to receive the FIR setting (0..7)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_fir(mlx90614_handle_t mlx90614_handle, uint8_t *fir);

/**
 * @brief Set the FIR (filter) configuration in the device config register.
 *
 * Only certain values are valid; this helper writes the appropriate bits to
 * the config register while preserving other fields.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] value FIR filter value to set
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_set_fir(mlx90614_handle_t mlx90614_handle, uint8_t value);

/**
 * @brief Get the IIR (infinite impulse response) filter setting.
 *
 * Returns the IIR value extracted from the configuration register.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[out] iir Pointer to receive the IIR setting (0..7)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_get_iir(mlx90614_handle_t mlx90614_handle, uint8_t *iir);

/**
 * @brief Set the IIR filter setting in the device configuration register.
 *
 * Writes the lower bits of the config register to set the IIR value while
 * preserving unrelated fields.
 *
 * @param[in] mlx90614_handle MLX90614 handle
 * @param[in] value IIR filter value to set (0..7)
 * @return ESP_OK on success, or an ESP error code on failure.
 */
esp_err_t mlx90614_set_iir(mlx90614_handle_t mlx90614_handle, uint8_t value);

/**
 * @brief Convert temperature in Celsius to Fahrenheit.
 *
 * @param[in] temperature Temperature in degrees Celsius
 * @return Temperature converted to degrees Fahrenheit
 */
float mlx90614_temperature_in_fahrenheit(float temperature);

/**
 * @brief Convert a raw 16-bit IR value to a signed 16-bit representation.
 *
 * The device returns IR data as a 16-bit unsigned value where values
 * greater than 0x7FFF represent negative numbers in two's complement.
 * This helper converts such raw data into a signed int16_t.
 *
 * @param[in] ir Raw 16-bit IR value from device
 * @return Signed 16-bit IR value
 */
int16_t mlx90614_convert_ir_data(uint16_t ir);

#ifdef __cplusplus
}
#endif

#endif  /* PSU_EXT_I2C_1_MODULES_MLX90614_VENDOR_MLX90614_H_ */
