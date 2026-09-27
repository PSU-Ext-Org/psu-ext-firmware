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
#include "ssd1306.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"
#include "output_ctrl.h"

enum {
    DISPLAY_WIDTH = 128,
    DISPLAY_PAGES = 8,
    TEXT_WIDTH = DISPLAY_WIDTH / 2,
    GLYPH_WIDTH = 3,
    CHARACTER_WIDTH = 4,
    I2C_TIMEOUT_MS = 50,
    REFRESH_MS = 200,
    CHART_SAMPLES = TEXT_WIDTH - 1,
    CHART_TOP = 8,
    CHART_BOTTOM = 62,
};

static const char *TAG = "ssd1306";

/* app_main initializes the bus, then transfers ownership to the display task. */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;

/* Cache the startup result so repeated calls never probe or restart the task. */
static bool s_init_attempted;
static bool s_init_succeeded;

/* Owned by the display task; each slot represents one 200 ms refresh. */
static uint32_t s_chart_values[CHART_SAMPLES];
static bool s_chart_valid[CHART_SAMPLES];

/* Compact 3x5 column glyphs with one blank column between characters. */
static const char GLYPH_CHARS[] = "0123456789VIAPWRELYONF :.-DCS";
static const uint8_t GLYPHS[][3] = {
    {0x1f, 0x11, 0x1f}, /* 0 */
    {0x12, 0x1f, 0x10}, /* 1 */
    {0x1d, 0x15, 0x17}, /* 2 */
    {0x15, 0x15, 0x1f}, /* 3 */
    {0x07, 0x04, 0x1f}, /* 4 */
    {0x17, 0x15, 0x1d}, /* 5 */
    {0x1f, 0x15, 0x1d}, /* 6 */
    {0x01, 0x1d, 0x03}, /* 7 */
    {0x1f, 0x15, 0x1f}, /* 8 */
    {0x17, 0x15, 0x1f}, /* 9 */
    {0x0f, 0x10, 0x0f}, /* V */
    {0x11, 0x1f, 0x11}, /* I */
    {0x1e, 0x05, 0x1e}, /* A */
    {0x1f, 0x05, 0x07}, /* P */
    {0x1f, 0x0c, 0x1f}, /* W */
    {0x1f, 0x0d, 0x16}, /* R */
    {0x1f, 0x15, 0x11}, /* E */
    {0x1f, 0x10, 0x10}, /* L */
    {0x03, 0x1c, 0x03}, /* Y */
    {0x1f, 0x11, 0x1f}, /* O */
    {0x1f, 0x06, 0x1f}, /* N */
    {0x1f, 0x05, 0x01}, /* F */
    {0x00, 0x00, 0x00}, /* space */
    {0x00, 0x0a, 0x00}, /* : */
    {0x00, 0x10, 0x00}, /* . */
    {0x04, 0x04, 0x04}, /* - */
    {0x1f, 0x11, 0x0e}, /* D */
    {0x1f, 0x11, 0x11}, /* C */
    {0x17, 0x15, 0x1d}, /* S */
};

/* Remove the device before deleting its bus; retain handles if removal fails. */
static void release_bus(void)
{
    if (s_device != NULL) {
        if (i2c_master_bus_rm_device(s_device) != ESP_OK) {
            return;
        }
        s_device = NULL;
    }
    if (s_bus != NULL) {
        if (i2c_del_master_bus(s_bus) == ESP_OK) {
            s_bus = NULL;
        }
    }
}

/* Write a page region without touching the other half of the display. */
static esp_err_t write_page(uint8_t page, uint8_t column, const uint8_t *pixels, size_t width)
{
    /* Control byte 0x00 selects commands: page and low/high column nibbles. */
    const uint8_t position[] = {
        0x00, (uint8_t)(0xb0 | page),
        (uint8_t)(column & 0x0f), (uint8_t)(0x10 | (column >> 4)),
    };
    esp_err_t err = i2c_master_transmit(s_device, position, sizeof(position), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    /* Control byte 0x40 selects display RAM data. */
    uint8_t payload[DISPLAY_WIDTH + 1] = {0x40};
    memcpy(payload + 1, pixels, width);
    return i2c_master_transmit(s_device, payload, width + 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_text(uint8_t page, const char *text)
{
    /* Erase the complete left row, including any suffix from the previous value. */
    uint8_t pixels[TEXT_WIDTH] = {0};
    for (size_t i = 0; (i < TEXT_WIDTH / CHARACTER_WIDTH) && (text[i] != '\0'); ++i) {
        const char *glyph = strchr(GLYPH_CHARS, text[i]);
        if (glyph != NULL) {
            memcpy(pixels + i * CHARACTER_WIDTH, GLYPHS[glyph - GLYPH_CHARS], GLYPH_WIDTH);
        }
    }
    return write_page(page, 0, pixels, sizeof(pixels));
}

static void append_chart_sample(uint32_t value_u4, bool valid)
{
    memmove(s_chart_values, s_chart_values + 1,
            (CHART_SAMPLES - 1) * sizeof(s_chart_values[0]));
    memmove(s_chart_valid, s_chart_valid + 1,
            (CHART_SAMPLES - 1) * sizeof(s_chart_valid[0]));
    s_chart_values[CHART_SAMPLES - 1] = value_u4;
    s_chart_valid[CHART_SAMPLES - 1] = valid;
}

static void chart_pixel(uint8_t pixels[DISPLAY_PAGES][TEXT_WIDTH], int x, int y)
{
    pixels[y / 8][x] |= (uint8_t)(1U << (y % 8));
}

static esp_err_t write_chart(void)
{
    uint8_t pixels[DISPLAY_PAGES][TEXT_WIDTH] = {0};
    uint32_t peak_u4 = 0;
    for (size_t i = 0; i < CHART_SAMPLES; ++i) {
        if (s_chart_valid[i] && (s_chart_values[i] > peak_u4)) {
            peak_u4 = s_chart_values[i];
        }
    }

    /* Zero-based scale, rounded up to whole volts, with a minimum 1 V range. */
    uint32_t top_volts = peak_u4 / 10000U + ((peak_u4 % 10000U) != 0U);
    if (top_volts == 0U) {
        top_volts = 1U;
    }
    const uint64_t top_u4 = (uint64_t)top_volts * 10000U;
    char title[20];
    snprintf(title, sizeof(title), "V1 0-%luV", (unsigned long)top_volts);
    for (size_t i = 0; (i < TEXT_WIDTH / CHARACTER_WIDTH) && (title[i] != '\0'); ++i) {
        const char *glyph = strchr(GLYPH_CHARS, title[i]);
        if (glyph != NULL) {
            memcpy(pixels[0] + i * CHARACTER_WIDTH,
                   GLYPHS[glyph - GLYPH_CHARS], GLYPH_WIDTH);
        }
    }

    for (int y = CHART_TOP; y < DISPLAY_PAGES * 8; ++y) {
        chart_pixel(pixels, 0, y);
    }
    for (int x = 0; x < TEXT_WIDTH; ++x) {
        chart_pixel(pixels, x, DISPLAY_PAGES * 8 - 1);
    }

    int previous_y = CHART_BOTTOM;
    bool previous_valid = false;
    for (int i = 0; i < CHART_SAMPLES; ++i) {
        if (!s_chart_valid[i]) {
            previous_valid = false; /* Missing readings leave a gap in the trace. */
            continue;
        }
        const int y = CHART_BOTTOM - (int)(
            ((uint64_t)s_chart_values[i] * (CHART_BOTTOM - CHART_TOP) + top_u4 / 2U) /
            top_u4);
        chart_pixel(pixels, i + 1, y);
        if (previous_valid) {
            /* Adjacent samples are one column apart; join their vertical span. */
            const int step = previous_y < y ? 1 : -1;
            for (int line_y = previous_y; line_y != y; line_y += step) {
                chart_pixel(pixels, i + 1, line_y);
            }
        }
        previous_y = y;
        previous_valid = true;
    }

    for (uint8_t page = 0; page < DISPLAY_PAGES; ++page) {
        esp_err_t err = write_page(page, TEXT_WIDTH, pixels[page], TEXT_WIDTH);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t write_reading(
    uint8_t page, measure_channel_t channel, measure_kind_t kind, char label, char unit)
{
    uint32_t value_u4 = 0;
    char text[24];

    /* u4 stores V, A, or W multiplied by 10,000. Even UINT32_MAX fits in 16 cells. */
    const bool valid = measure_svc_read(channel, kind, &value_u4) == ESP_OK;
    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        append_chart_sample(value_u4, valid);
    }
    if (valid) {
        snprintf(
            text, sizeof(text), "%c%u: %lu.%04lu%c", label, (unsigned int)channel,
            (unsigned long)(value_u4 / 10000U),
            (unsigned long)(value_u4 % 10000U), unit);
    } else {
        snprintf(text, sizeof(text), "%c%u: ----%c", label, (unsigned int)channel, unit);
    }

    return write_text(page, text);
}

static void display_task(void *context)
{
    (void)context;
    TickType_t wake_time = xTaskGetTickCount();

    for (;;) {
        /* Report the relay's commanded state, not physical contact feedback. */
        bool relay_enabled;
        const char *relay_text = "RELAY: ----";
        if (output_ctrl_get(1, &relay_enabled) == ESP_OK) {
            relay_text = relay_enabled ? "RELAY: ON" : "RELAY: OFF";
        }
        esp_err_t err = write_text(0, relay_text);
        if (err == ESP_OK) {
            err = write_reading(1, MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 'V', 'V');
        }
        if (err == ESP_OK) {
            err = write_reading(2, MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 'V', 'V');
        }
        if (err == ESP_OK) {
            err = write_reading(3, MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 'I', 'A');
        }
        if (err == ESP_OK) {
            err = write_reading(4, MEASURE_CHANNEL_1, MEASURE_KIND_POWER, 'P', 'W');
        }
        if (err == ESP_OK) {
            /* Read each refresh so runtime ADC rate changes appear immediately. */
            uint16_t rate_sps;
            char rate_text[20];
            if (measure_svc_get_adc_data_rate_sps(&rate_sps) == ESP_OK) {
                snprintf(rate_text, sizeof(rate_text), "ADC: %u SPS", (unsigned int)rate_sps);
            } else {
                snprintf(rate_text, sizeof(rate_text), "ADC: ---- SPS");
            }
            err = write_text(5, rate_text);
        }
        if (err == ESP_OK) {
            err = write_chart();
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Display disabled until reboot: %s", esp_err_to_name(err));
            release_bus();
            vTaskDelete(NULL);
            return;
        }
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(REFRESH_MS));
    }
}

static esp_err_t initialize_display(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = GPIO_NUM_6,
        .scl_io_num = GPIO_NUM_7,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    /* Prefer 0x3C. Only an address NACK permits trying the alternate address. */
    uint8_t address;
    for (address = 0x3c; address <= 0x3d; ++address) {
        err = i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
        if (err != ESP_ERR_NOT_FOUND) {
            return err;
        }
    }
    if (address > 0x3d) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        return err;
    }

    /* Keep the screen off until RAM is cleared. C8/A1 match the reference. */
    const uint8_t init[] = {
        0x00,       /* Command control byte. */
        0xae,       /* Display off. */
        0x20, 0x02, /* Page addressing mode. */
        0xc8,       /* Scan COM outputs in reverse order. */
        0xa1,       /* Map column 127 to SEG0. */
        0x40,       /* Display start line zero. */
        0x81, 0x7f, /* Contrast. */
        0xa6,       /* Normal, non-inverted pixels. */
        0xa8, 0x3f, /* Multiplex all 64 rows. */
        0xa4,       /* Display follows RAM contents. */
        0xd3, 0x00, /* No vertical offset. */
        0xd5, 0x80, /* Display clock and oscillator. */
        0xd9, 0x22, /* Pre-charge periods. */
        0xda, 0x12, /* COM pin configuration for the 128x64 panel. */
        0xdb, 0x20, /* VCOMH deselect level. */
        0x8d, 0x14, /* Enable the internal charge pump. */
        0x2e,       /* Disable scrolling. */
    };
    err = i2c_master_transmit(s_device, init, sizeof(init), I2C_TIMEOUT_MS);
    const uint8_t blank[DISPLAY_WIDTH] = {0};
    for (uint8_t page = 0; (err == ESP_OK) && (page < DISPLAY_PAGES); ++page) {
        err = write_page(page, 0, blank, sizeof(blank));
    }
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t display_on[] = {0x00, 0xaf};
    err = i2c_master_transmit(s_device, display_on, sizeof(display_on), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(display_task, "ssd1306", 3072, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ssd1306_init(void)
{
    if (!s_init_attempted) {
        s_init_attempted = true;
        const esp_err_t err = initialize_display();
        s_init_succeeded = (err == ESP_OK);
        if (!s_init_succeeded) {
            release_bus();
            if (err == ESP_ERR_NOT_FOUND) {
                ESP_LOGI(TAG, "Optional display not found");
            } else {
                ESP_LOGW(TAG, "Display initialization failed: %s", esp_err_to_name(err));
            }
        }
    }
    return s_init_succeeded;
}
