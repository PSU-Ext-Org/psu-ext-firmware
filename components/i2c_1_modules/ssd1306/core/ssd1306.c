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
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306_views_internal.h"
#include "ssd1306_refresh.h"

enum {
    I2C_TIMEOUT_MS = 50,
    REFRESH_MS = 200,
};

static const char *TAG = "ssd1306";

static i2c_master_dev_handle_t s_device;

/* Cache the startup result so repeated calls never probe or restart the task. */
static bool s_init_attempted;
static esp_err_t s_init_result;

/* Only the display task writes the framebuffer. */
static ssd1306_frame_t s_frame;
/* Static storage keeps the extra 1 KB off the display task's stack. */
static ssd1306_page_cache_t s_page_cache;

/* The ISR records only edges and wakes the task; it never touches I2C or views. */
static portMUX_TYPE s_button_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_display_task;
static bool s_button_pressed;
static uint32_t s_button_edge_ms;
static uint32_t s_button_sequence;
static bool s_button_handler_added;

static void button_edge_isr(void *argument)
{
    (void)argument;
    BaseType_t task_woken = pdFALSE;
    portENTER_CRITICAL_ISR(&s_button_lock);
    s_button_pressed = gpio_get_level(GPIO_NUM_0) == 0;
    s_button_edge_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ++s_button_sequence;
    if (s_display_task != NULL) {
        xTaskNotifyFromISR(s_display_task, 1U, eSetBits, &task_woken);
    }
    portEXIT_CRITICAL_ISR(&s_button_lock);
    if (task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void release_button(void)
{
    /* Synchronize with any in-flight ISR before allowing the task to be deleted. */
    portENTER_CRITICAL(&s_button_lock);
    s_display_task = NULL;
    portEXIT_CRITICAL(&s_button_lock);
    if (s_button_handler_added) {
        gpio_isr_handler_remove(GPIO_NUM_0);
        s_button_handler_added = false;
    }
    /* The GPIO ISR service is shared with trigger_svc; never uninstall it here. */
}

/* Only the display device is owned here; the manager retains the shared bus. */
static void release_device(void)
{
    if (s_device != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_device);
        if (err == ESP_OK) {
            s_device = NULL;
        } else {
            ESP_LOGW(TAG, "Display device cleanup failed: %s", esp_err_to_name(err));
        }
    }
}

/* Write one complete page from the task-owned framebuffer. */
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
    uint8_t payload[SSD1306_WIDTH + 1] = {0x40};
    memcpy(payload + 1, pixels, width);
    return i2c_master_transmit(s_device, payload, width + 1, I2C_TIMEOUT_MS);
}

static void display_task(void *context)
{
    (void)context;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t refreshed_ms = now_ms - REFRESH_MS;
    unsigned rendered_view = 0;
    ssd1306_button_t button;
    portENTER_CRITICAL(&s_button_lock);
    s_display_task = xTaskGetCurrentTaskHandle();
    s_button_pressed = gpio_get_level(GPIO_NUM_0) == 0;
    uint32_t seen_sequence = s_button_sequence;
    ssd1306_button_init(&button, s_button_pressed, now_ms);
    portEXIT_CRITICAL(&s_button_lock);
    /* Publish availability from the task so an immediate I2C failure cannot
     * race with the startup caller and accidentally mark a stopped task ready. */
    ssd1306_views_set_available(true);
    for (;;) {
        now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        portENTER_CRITICAL(&s_button_lock);
        if (seen_sequence != s_button_sequence) {
            /* Reset the deadline on every captured edge, even when multiple
             * bounces ended at the same level while a frame was transmitting. */
            ssd1306_button_edge(&button, s_button_pressed, s_button_edge_ms);
            seen_sequence = s_button_sequence;
        }
        portEXIT_CRITICAL(&s_button_lock);
        now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ssd1306_button_debounce(&button, now_ms);
        unsigned view = ssd1306_views_get_selected();
        if (view != rendered_view || (uint32_t)(now_ms - refreshed_ms) >= REFRESH_MS) {
            ssd1306_views_render(view, s_frame);
            esp_err_t err = ssd1306_refresh_pages(&s_page_cache, s_frame, write_page);
            if (err != ESP_OK) {
                ssd1306_views_set_available(false);
                ESP_LOGW(TAG, "Display disabled until reboot: %s", esp_err_to_name(err));
                release_button();
                release_device();
                vTaskDelete(NULL);
                return;
            }
            rendered_view = view;
            refreshed_ms = now_ms;
        }
        now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t elapsed_ms = now_ms - refreshed_ms;
        uint32_t wait_ms = elapsed_ms < REFRESH_MS ? REFRESH_MS - elapsed_ms : 0;
        if (button.candidate_pressed != button.stable_pressed) {
            elapsed_ms = now_ms - button.candidate_since_ms;
            uint32_t debounce_ms = elapsed_ms < SSD1306_BUTTON_DEBOUNCE_MS ? SSD1306_BUTTON_DEBOUNCE_MS - elapsed_ms : 0;
            if (debounce_ms < wait_ms) {
                wait_ms = debounce_ms;
            }
        }
        if (wait_ms != 0) {
            TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
            uint32_t events;
            xTaskNotifyWait(0, UINT32_MAX, &events, wait_ticks > 0 ? wait_ticks : 1);
        }
    }
}

static esp_err_t initialize_display(i2c_master_bus_handle_t bus)
{
    esp_err_t err;

    /* Prefer 0x3C. Only an address NACK permits trying the alternate address. */
    uint8_t address;
    for (address = 0x3c; address <= 0x3d; ++address) {
        err = i2c_master_probe(bus, address, I2C_TIMEOUT_MS);
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
    err = i2c_master_bus_add_device(bus, &device_config, &s_device);
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
    const uint8_t blank[SSD1306_WIDTH] = {0};
    for (uint8_t page = 0; (err == ESP_OK) && (page < SSD1306_PAGES); ++page) {
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
    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    err = gpio_config(&button_config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_isr_handler_add(GPIO_NUM_0, button_edge_isr, NULL);
    if (err != ESP_OK) {
        return err;
    }
    s_button_handler_added = true;
    if (xTaskCreate(display_task, "ssd1306", 4096, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        release_button();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus)
{
    if (!s_init_attempted) {
        s_init_attempted = true;
        s_init_result = (bus == NULL) ? ESP_ERR_INVALID_ARG : initialize_display(bus);
        if (s_init_result != ESP_OK) {
            release_button();
            release_device();
            ESP_LOGW(TAG, "Display initialization failed: %s", esp_err_to_name(s_init_result));
        } else {
            ESP_LOGI(TAG, "SSD1306 display located on SDA6/SCL7");
        }
    }
    return s_init_result;
}
