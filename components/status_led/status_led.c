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
 * @file status_led.c
 * @brief Plain USB and WiFi status LED control.
 */

#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STATUS_LED_USB_GPIO GPIO_NUM_17
#define STATUS_LED_WIFI_GPIO GPIO_NUM_18
#define STATUS_LED_TASK_STACK_SIZE 3072
#define STATUS_LED_TASK_PRIORITY 3
#define STATUS_LED_UPDATE_PERIOD_MS 50
#define STATUS_LED_LEDC_TIMER LEDC_TIMER_0
#define STATUS_LED_LEDC_MODE LEDC_LOW_SPEED_MODE
#define STATUS_LED_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define STATUS_LED_LEDC_FREQ_HZ 5000
#define STATUS_LED_USB_CHANNEL LEDC_CHANNEL_0
#define STATUS_LED_WIFI_CHANNEL LEDC_CHANNEL_1
#define STATUS_LED_DUTY_MAX 1023U
#define STATUS_LED_SOLID_DUTY 320U
#define STATUS_LED_BREATH_MIN_DUTY 24U
#define STATUS_LED_BREATH_MAX_DUTY 360U
#define STATUS_LED_BREATH_PERIOD_MS 2000U
#define STATUS_LED_FAIL_BLINK_ON_MS 120U
#define STATUS_LED_FAIL_BLINK_OFF_MS 120U
#define STATUS_LED_FAIL_BLINK_COUNT 3U
#define STATUS_LED_FAIL_PAUSE_MS 2000U

typedef enum {
    STATUS_LED_KIND_USB,
    STATUS_LED_KIND_WIFI,
} status_led_kind_t;

static const char *TAG = "status_led";

static struct {
    bool initialized;
    volatile bool usb_connected;
    volatile bool usb_failed;
    volatile bool wifi_connected;
    volatile bool wifi_connecting;
    volatile bool wifi_failed;
    TaskHandle_t task_handle;
    uint32_t usb_duty;
    uint32_t wifi_duty;
    bool usb_duty_valid;
    bool wifi_duty_valid;
} s_status_led;

static uint32_t status_led_clamp_duty(uint32_t duty)
{
    return duty > STATUS_LED_DUTY_MAX ? STATUS_LED_DUTY_MAX : duty;
}

static bool status_led_is_failure_blink_on(uint32_t now_ms)
{
    const uint32_t blink_slot_ms = STATUS_LED_FAIL_BLINK_ON_MS + STATUS_LED_FAIL_BLINK_OFF_MS;
    const uint32_t blink_window_ms = blink_slot_ms * STATUS_LED_FAIL_BLINK_COUNT;
    const uint32_t cycle_ms = blink_window_ms + STATUS_LED_FAIL_PAUSE_MS;
    const uint32_t phase_ms = now_ms % cycle_ms;

    if (phase_ms >= blink_window_ms) {
        return false;
    }

    return (phase_ms % blink_slot_ms) < STATUS_LED_FAIL_BLINK_ON_MS;
}

static uint32_t status_led_get_breath_duty(uint32_t now_ms)
{
    const uint32_t half_period_ms = STATUS_LED_BREATH_PERIOD_MS / 2U;
    const uint32_t phase_ms = now_ms % STATUS_LED_BREATH_PERIOD_MS;
    const uint32_t ramp_ms = phase_ms < half_period_ms ? phase_ms : STATUS_LED_BREATH_PERIOD_MS - phase_ms;
    const uint32_t span = STATUS_LED_BREATH_MAX_DUTY - STATUS_LED_BREATH_MIN_DUTY;

    return STATUS_LED_BREATH_MIN_DUTY + ((span * ramp_ms) / half_period_ms);
}

static uint32_t status_led_get_duty(status_led_kind_t kind, uint32_t now_ms)
{
    if (kind == STATUS_LED_KIND_USB) {
        if (s_status_led.usb_failed) {
            return status_led_is_failure_blink_on(now_ms) ? STATUS_LED_SOLID_DUTY : 0U;
        }

        return s_status_led.usb_connected ? STATUS_LED_SOLID_DUTY : 0U;
    }

    if (s_status_led.wifi_failed) {
        return status_led_is_failure_blink_on(now_ms) ? STATUS_LED_SOLID_DUTY : 0U;
    }

    if (s_status_led.wifi_connected) {
        return STATUS_LED_SOLID_DUTY;
    }

    if (s_status_led.wifi_connecting) {
        return status_led_get_breath_duty(now_ms);
    }

    return 0U;
}

static void status_led_set_channel_duty(
    ledc_channel_t channel,
    uint32_t duty,
    uint32_t *cached_duty,
    bool *cached_duty_valid)
{
    const uint32_t clamped_duty = status_led_clamp_duty(duty);

    if ((cached_duty != NULL) && (cached_duty_valid != NULL) &&
        *cached_duty_valid && (*cached_duty == clamped_duty)) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(STATUS_LED_LEDC_MODE, channel, clamped_duty));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(STATUS_LED_LEDC_MODE, channel));

    if ((cached_duty != NULL) && (cached_duty_valid != NULL)) {
        *cached_duty = clamped_duty;
        *cached_duty_valid = true;
    }
}

static void status_led_task(void *arg)
{
    (void)arg;

    for (;;) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        status_led_set_channel_duty(
            STATUS_LED_USB_CHANNEL,
            status_led_get_duty(STATUS_LED_KIND_USB, now_ms),
            &s_status_led.usb_duty,
            &s_status_led.usb_duty_valid);
        status_led_set_channel_duty(
            STATUS_LED_WIFI_CHANNEL,
            status_led_get_duty(STATUS_LED_KIND_WIFI, now_ms),
            &s_status_led.wifi_duty,
            &s_status_led.wifi_duty_valid);

        vTaskDelay(pdMS_TO_TICKS(STATUS_LED_UPDATE_PERIOD_MS));
    }
}

static esp_err_t status_led_configure_channel(ledc_channel_t channel, gpio_num_t gpio)
{
    ledc_channel_config_t channel_config = {
        .gpio_num = gpio,
        .speed_mode = STATUS_LED_LEDC_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = STATUS_LED_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    return ledc_channel_config(&channel_config);
}

esp_err_t status_led_init(void)
{
    if (s_status_led.initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = STATUS_LED_LEDC_MODE,
        .duty_resolution = STATUS_LED_LEDC_DUTY_RES,
        .timer_num = STATUS_LED_LEDC_TIMER,
        .freq_hz = STATUS_LED_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "LEDC timer init failed");
    ESP_RETURN_ON_ERROR(status_led_configure_channel(STATUS_LED_USB_CHANNEL, STATUS_LED_USB_GPIO), TAG, "USB LED init failed");
    ESP_RETURN_ON_ERROR(status_led_configure_channel(STATUS_LED_WIFI_CHANNEL, STATUS_LED_WIFI_GPIO), TAG, "WiFi LED init failed");

    s_status_led.usb_connected = false;
    s_status_led.usb_failed = false;
    s_status_led.wifi_connected = false;
    s_status_led.wifi_connecting = false;
    s_status_led.wifi_failed = false;
    s_status_led.usb_duty = 0U;
    s_status_led.wifi_duty = 0U;
    s_status_led.usb_duty_valid = false;
    s_status_led.wifi_duty_valid = false;

    BaseType_t task_created = xTaskCreate(
        status_led_task,
        "status_led",
        STATUS_LED_TASK_STACK_SIZE,
        NULL,
        STATUS_LED_TASK_PRIORITY,
        &s_status_led.task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "status LED task creation failed");

    s_status_led.initialized = true;
    ESP_LOGI(TAG, "Status LEDs initialized: USB GPIO %d, WiFi GPIO %d", STATUS_LED_USB_GPIO, STATUS_LED_WIFI_GPIO);
    return ESP_OK;
}

void status_led_set_usb_connected(bool connected)
{
    s_status_led.usb_connected = connected;
    if (connected) {
        s_status_led.usb_failed = false;
    }
}

void status_led_set_usb_failed(bool failed)
{
    s_status_led.usb_failed = failed;
    if (failed) {
        s_status_led.usb_connected = false;
    }
}

void status_led_set_wifi_connected(bool connected)
{
    s_status_led.wifi_connected = connected;
    if (connected) {
        s_status_led.wifi_connecting = false;
        s_status_led.wifi_failed = false;
    }
}

void status_led_set_wifi_connecting(bool connecting)
{
    s_status_led.wifi_connecting = connecting;
    if (connecting) {
        s_status_led.wifi_connected = false;
        s_status_led.wifi_failed = false;
    }
}

void status_led_set_wifi_failed(bool failed)
{
    s_status_led.wifi_failed = failed;
    if (failed) {
        s_status_led.wifi_connected = false;
        s_status_led.wifi_connecting = false;
    }
}
