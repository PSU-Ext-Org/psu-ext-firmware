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
 * @file output_ctrl.c
 * @brief GPIO-backed output control for PSU output relays.
 */

#include "output_ctrl.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define OUTPUT_CTRL_CH1 1U
#define OUTPUT_CTRL_CH1_GPIO GPIO_NUM_38
#define OUTPUT_CTRL_MAX_LISTENERS 4U

static const char *TAG = "output_ctrl";

typedef struct {
    output_ctrl_listener_fn_t callback;
    void *context;
} output_ctrl_listener_entry_t;

static struct {
    bool initialized;
    bool ch1_enabled;
    SemaphoreHandle_t lock;
    output_ctrl_listener_entry_t listeners[OUTPUT_CTRL_MAX_LISTENERS];
    size_t listener_count;
} s_output_ctrl;

static bool output_ctrl_channel_is_supported(uint8_t channel)
{
    return channel == OUTPUT_CTRL_CH1;
}

esp_err_t output_ctrl_init(void)
{
    if (s_output_ctrl.initialized) {
        return ESP_OK;
    }

    if (s_output_ctrl.lock == NULL) {
        s_output_ctrl.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_output_ctrl.lock != NULL, ESP_ERR_NO_MEM, TAG, "listener lock alloc failed");
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(OUTPUT_CTRL_CH1_GPIO, 0), TAG, "CH1 output default-off set failed");

    gpio_config_t config = {
        .pin_bit_mask = BIT64(OUTPUT_CTRL_CH1_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "CH1 output GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(OUTPUT_CTRL_CH1_GPIO, 0), TAG, "CH1 output off set failed");

    s_output_ctrl.ch1_enabled = false;
    s_output_ctrl.initialized = true;

    ESP_LOGI(TAG, "Output CH1 initialized on GPIO %d, default off", OUTPUT_CTRL_CH1_GPIO);
    return ESP_OK;
}

static void output_ctrl_notify_listeners(
    const output_ctrl_change_event_t *event)
{
    output_ctrl_listener_entry_t listeners[OUTPUT_CTRL_MAX_LISTENERS];
    size_t listener_count = 0U;

    if ((event == NULL) || (s_output_ctrl.lock == NULL)) {
        return;
    }

    if (xSemaphoreTake(s_output_ctrl.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    listener_count = s_output_ctrl.listener_count;
    for (size_t i = 0; i < listener_count; ++i) {
        listeners[i] = s_output_ctrl.listeners[i];
    }

    xSemaphoreGive(s_output_ctrl.lock);

    for (size_t i = 0; i < listener_count; ++i) {
        if (listeners[i].callback != NULL) {
            listeners[i].callback(event, listeners[i].context);
        }
    }
}

esp_err_t output_ctrl_set_with_cause(
    uint8_t channel,
    bool enabled,
    output_ctrl_change_cause_t cause)
{
    ESP_RETURN_ON_FALSE(output_ctrl_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported output channel");
    ESP_RETURN_ON_FALSE(s_output_ctrl.initialized, ESP_ERR_INVALID_STATE, TAG, "output control not initialized");

    ESP_RETURN_ON_ERROR(gpio_set_level(OUTPUT_CTRL_CH1_GPIO, enabled ? 1 : 0), TAG, "CH1 output set failed");
    s_output_ctrl.ch1_enabled = enabled;

    const output_ctrl_change_event_t event = {
        .channel = channel,
        .enabled = enabled,
        .cause = cause,
    };
    output_ctrl_notify_listeners(&event);
    return ESP_OK;
}

esp_err_t output_ctrl_set(uint8_t channel, bool enabled)
{
    return output_ctrl_set_with_cause(channel, enabled, OUTPUT_CTRL_CHANGE_CAUSE_SCPI);
}

esp_err_t output_ctrl_get(uint8_t channel, bool *enabled)
{
    ESP_RETURN_ON_FALSE(output_ctrl_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported output channel");
    ESP_RETURN_ON_FALSE(enabled != NULL, ESP_ERR_INVALID_ARG, TAG, "enabled pointer is null");
    ESP_RETURN_ON_FALSE(s_output_ctrl.initialized, ESP_ERR_INVALID_STATE, TAG, "output control not initialized");

    *enabled = s_output_ctrl.ch1_enabled;
    return ESP_OK;
}

esp_err_t output_ctrl_register_listener(
    output_ctrl_listener_fn_t listener,
    void *context)
{
    ESP_RETURN_ON_FALSE(listener != NULL, ESP_ERR_INVALID_ARG, TAG, "listener is null");
    ESP_RETURN_ON_FALSE(s_output_ctrl.initialized, ESP_ERR_INVALID_STATE, TAG, "output control not initialized");
    ESP_RETURN_ON_FALSE(s_output_ctrl.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "listener lock not initialized");

    if (xSemaphoreTake(s_output_ctrl.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < s_output_ctrl.listener_count; ++i) {
        if ((s_output_ctrl.listeners[i].callback == listener) &&
            (s_output_ctrl.listeners[i].context == context)) {
            xSemaphoreGive(s_output_ctrl.lock);
            return ESP_OK;
        }
    }

    if (s_output_ctrl.listener_count >= OUTPUT_CTRL_MAX_LISTENERS) {
        xSemaphoreGive(s_output_ctrl.lock);
        return ESP_ERR_NO_MEM;
    }

    s_output_ctrl.listeners[s_output_ctrl.listener_count].callback = listener;
    s_output_ctrl.listeners[s_output_ctrl.listener_count].context = context;
    s_output_ctrl.listener_count++;
    xSemaphoreGive(s_output_ctrl.lock);
    return ESP_OK;
}
