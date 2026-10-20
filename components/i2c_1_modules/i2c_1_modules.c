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
#include "i2c_1_modules.h"

#include <stdbool.h>

#include "sdkconfig.h"

#ifdef CONFIG_PSUEXT_I2C_1_ENABLED
#include "driver/i2c_master.h"

/* Owned by this manager for the remainder of the boot, even if a module fails. */
static i2c_master_bus_handle_t s_bus;
#endif

#ifdef CONFIG_PSUEXT_I2C_1_SSD1306
#include "ssd1306.h"
#include "ssd1306_views_internal.h"
#endif

static bool s_init_attempted;
static esp_err_t s_init_result;

esp_err_t i2c_1_modules_init(void)
{
    if (s_init_attempted) {
        return s_init_result;
    }
    s_init_attempted = true;
    s_init_result = ESP_OK;

#ifdef CONFIG_PSUEXT_I2C_1_SSD1306
    /* Keep commands available to report hardware/bus failures explicitly. */
    s_init_result = ssd1306_register_scpi_commands();
#endif

#ifdef CONFIG_PSUEXT_I2C_1_ENABLED
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = GPIO_NUM_6,
        .scl_io_num = GPIO_NUM_7,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    const esp_err_t bus_result = i2c_new_master_bus(&bus_config, &s_bus);
    if (bus_result != ESP_OK) {
        if (s_init_result == ESP_OK) {
            s_init_result = bus_result;
        }
        return s_init_result;
    }

#endif

#ifdef CONFIG_PSUEXT_I2C_1_SSD1306
    /* Record the first module error without preventing later module attempts. */
    const esp_err_t display_result = ssd1306_init(s_bus);
    if (s_init_result == ESP_OK) {
        s_init_result = display_result;
    }
#endif

    return s_init_result;
}
