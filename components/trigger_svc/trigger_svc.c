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
 * @file trigger_svc.c
 * @brief Debounced external trigger service for GPIO-backed actions.
 */

#include "trigger_svc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TRIGGER_SVC_PIN_COUNT 2U
#define TRIGGER_SVC_STATE_COUNT 2U
#define TRIGGER_SVC_EVENT_QUEUE_LENGTH 8U
#define TRIGGER_SVC_TASK_STACK_WORDS 4096U
#define TRIGGER_SVC_TASK_PRIORITY 6U
#define TRIGGER_SVC_DEBOUNCE_MS 50U

static const char *TAG = "trigger_svc";

typedef struct {
    trigger_svc_pin_t pin;
    gpio_num_t gpio;
} trigger_svc_pin_config_t;

typedef struct {
    bool initialized;
    QueueHandle_t event_queue;
    TaskHandle_t worker_task;
    volatile bool debounce_pending[TRIGGER_SVC_PIN_COUNT];
    trigger_svc_state_t stable_state[TRIGGER_SVC_PIN_COUNT];
    uint32_t last_event_time_ms[TRIGGER_SVC_PIN_COUNT];
    const trigger_action_t *actions[TRIGGER_SVC_PIN_COUNT][TRIGGER_SVC_STATE_COUNT];
} trigger_svc_runtime_t;

static const trigger_svc_pin_config_t s_trigger_pins[TRIGGER_SVC_PIN_COUNT] = {
    { .pin = TRIGGER_SVC_PIN_IO4, .gpio = GPIO_NUM_4 },
    { .pin = TRIGGER_SVC_PIN_IO5, .gpio = GPIO_NUM_5 },
};

static trigger_svc_runtime_t s_trigger_svc;

static size_t trigger_svc_pin_to_index(trigger_svc_pin_t pin)
{
    switch (pin) {
    case TRIGGER_SVC_PIN_IO4:
        return 0U;
    case TRIGGER_SVC_PIN_IO5:
        return 1U;
    default:
        return TRIGGER_SVC_PIN_COUNT;
    }
}

static bool trigger_svc_state_is_valid(trigger_svc_state_t state)
{
    return (state == TRIGGER_SVC_STATE_LOW) || (state == TRIGGER_SVC_STATE_HIGH);
}

static const char *trigger_svc_state_name(trigger_svc_state_t state)
{
    return state == TRIGGER_SVC_STATE_LOW ? "LOW" : "HIGH";
}

static gpio_num_t trigger_svc_gpio_for_index(size_t pin_index)
{
    return s_trigger_pins[pin_index].gpio;
}

static trigger_svc_pin_t trigger_svc_pin_for_index(size_t pin_index)
{
    return s_trigger_pins[pin_index].pin;
}

static uint32_t trigger_svc_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static trigger_svc_state_t trigger_svc_state_from_gpio_level(int level)
{
    return level == 0 ? TRIGGER_SVC_STATE_LOW : TRIGGER_SVC_STATE_HIGH;
}

static esp_err_t trigger_svc_read_pin_state(size_t pin_index, trigger_svc_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const int level = gpio_get_level(trigger_svc_gpio_for_index(pin_index));
    if ((level != 0) && (level != 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *state = trigger_svc_state_from_gpio_level(level);
    return ESP_OK;
}

static void IRAM_ATTR trigger_svc_gpio_isr_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    const uint32_t pin_value = (uint32_t)(uintptr_t)arg;
    const size_t pin_index = trigger_svc_pin_to_index((trigger_svc_pin_t)pin_value);

    if ((pin_index >= TRIGGER_SVC_PIN_COUNT) || (s_trigger_svc.event_queue == NULL)) {
        return;
    }

    if (s_trigger_svc.debounce_pending[pin_index]) {
        return;
    }

    s_trigger_svc.debounce_pending[pin_index] = true;
    if (xQueueSendFromISR(s_trigger_svc.event_queue, &pin_value, &higher_priority_task_woken) != pdTRUE) {
        s_trigger_svc.debounce_pending[pin_index] = false;
    }

    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void trigger_svc_dispatch_action(
    size_t pin_index,
    const trigger_svc_event_t *event)
{
    const trigger_action_t *action;
    esp_err_t err;

    if (event == NULL) {
        return;
    }

    action = s_trigger_svc.actions[pin_index][event->state];
    if (action == NULL) {
        ESP_LOGD(
            TAG,
            "no action configured for pin IO%d state=%s",
            (int)event->pin,
            trigger_svc_state_name(event->state));
        return;
    }

    if (action->execute == NULL) {
        ESP_LOGW(
            TAG,
            "action %s for pin IO%d state=%s has no execute callback",
            action->name != NULL ? action->name : "UNNAMED",
            (int)event->pin,
            trigger_svc_state_name(event->state));
        return;
    }

    ESP_LOGI(
        TAG,
        "dispatching action %s for pin IO%d state=%s",
        action->name != NULL ? action->name : "UNNAMED",
        (int)event->pin,
        trigger_svc_state_name(event->state));

    err = action->execute(event);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "action %s failed for pin IO%d state=%s: %s",
            action->name != NULL ? action->name : "UNNAMED",
            (int)event->pin,
            trigger_svc_state_name(event->state),
            esp_err_to_name(err));
        return;
    }

    ESP_LOGI(
        TAG,
        "action %s completed for pin IO%d state=%s",
        action->name != NULL ? action->name : "UNNAMED",
        (int)event->pin,
        trigger_svc_state_name(event->state));
}

static void trigger_svc_worker_task(void *arg)
{
    uint32_t pin_value;

    (void)arg;

    while (true) {
        if (xQueueReceive(s_trigger_svc.event_queue, &pin_value, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const size_t pin_index = trigger_svc_pin_to_index((trigger_svc_pin_t)pin_value);
        if (pin_index >= TRIGGER_SVC_PIN_COUNT) {
            ESP_LOGW(TAG, "worker received unknown trigger pin value %lu", (unsigned long)pin_value);
            continue;
        }

        ESP_LOGD(TAG, "raw GPIO edge queued for pin IO%d", (int)pin_value);
        vTaskDelay(pdMS_TO_TICKS(TRIGGER_SVC_DEBOUNCE_MS));

        trigger_svc_state_t state;
        esp_err_t err = trigger_svc_read_pin_state(pin_index, &state);
        s_trigger_svc.debounce_pending[pin_index] = false;
        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "reading debounced level for pin IO%d failed: %s",
                (int)pin_value,
                esp_err_to_name(err));
            continue;
        }

        if (state == s_trigger_svc.stable_state[pin_index]) {
            ESP_LOGD(
                TAG,
                "ignored bounce/no-op on pin IO%d state=%s",
                (int)pin_value,
                trigger_svc_state_name(state));
            continue;
        }

        s_trigger_svc.stable_state[pin_index] = state;
        s_trigger_svc.last_event_time_ms[pin_index] = trigger_svc_time_ms();

        const trigger_svc_event_t event = {
            .pin = trigger_svc_pin_for_index(pin_index),
            .state = state,
            .time_ms = s_trigger_svc.last_event_time_ms[pin_index],
        };

        ESP_LOGI(
            TAG,
            "accepted debounced event pin IO%d state=%s at %lu ms",
            (int)event.pin,
            trigger_svc_state_name(event.state),
            (unsigned long)event.time_ms);
        trigger_svc_dispatch_action(pin_index, &event);
    }
}

static esp_err_t trigger_svc_init_queue_and_task(void)
{
    if (s_trigger_svc.event_queue == NULL) {
        s_trigger_svc.event_queue = xQueueCreate(
            TRIGGER_SVC_EVENT_QUEUE_LENGTH,
            sizeof(uint32_t));
        ESP_RETURN_ON_FALSE(s_trigger_svc.event_queue != NULL, ESP_ERR_NO_MEM, TAG, "event queue alloc failed");
    }

    if (s_trigger_svc.worker_task == NULL) {
        const BaseType_t created = xTaskCreate(
            trigger_svc_worker_task,
            "trigger_svc",
            TRIGGER_SVC_TASK_STACK_WORDS,
            NULL,
            TRIGGER_SVC_TASK_PRIORITY,
            &s_trigger_svc.worker_task);
        if (created != pdPASS) {
            s_trigger_svc.worker_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static esp_err_t trigger_svc_configure_pin(size_t pin_index)
{
    const gpio_num_t gpio = trigger_svc_gpio_for_index(pin_index);
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "GPIO config failed");
    return gpio_isr_handler_add(
        gpio,
        trigger_svc_gpio_isr_handler,
        (void *)(uintptr_t)trigger_svc_pin_for_index(pin_index));
}

esp_err_t trigger_svc_init(void)
{
    esp_err_t err;

    if (s_trigger_svc.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(trigger_svc_init_queue_and_task(), TAG, "trigger queue/task init failed");

    err = gpio_install_isr_service(0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < TRIGGER_SVC_PIN_COUNT; ++i) {
        ESP_RETURN_ON_ERROR(trigger_svc_configure_pin(i), TAG, "trigger pin config failed");
        ESP_RETURN_ON_ERROR(
            trigger_svc_read_pin_state(i, &s_trigger_svc.stable_state[i]),
            TAG,
            "initial trigger state read failed");
        s_trigger_svc.debounce_pending[i] = false;
        s_trigger_svc.last_event_time_ms[i] = trigger_svc_time_ms();

        ESP_LOGI(
            TAG,
            "configured pin IO%d initial state=%s debounce=%u ms",
            (int)trigger_svc_pin_for_index(i),
            trigger_svc_state_name(s_trigger_svc.stable_state[i]),
            TRIGGER_SVC_DEBOUNCE_MS);
    }

    s_trigger_svc.initialized = true;
    ESP_LOGI(TAG, "trigger service initialized for IO4 and IO5");
    return ESP_OK;
}

esp_err_t trigger_svc_set_action(
    trigger_svc_pin_t pin,
    trigger_svc_state_t state,
    const trigger_action_t *action)
{
    const size_t pin_index = trigger_svc_pin_to_index(pin);

    ESP_RETURN_ON_FALSE(pin_index < TRIGGER_SVC_PIN_COUNT, ESP_ERR_INVALID_ARG, TAG, "unsupported trigger pin");
    ESP_RETURN_ON_FALSE(trigger_svc_state_is_valid(state), ESP_ERR_INVALID_ARG, TAG, "unsupported trigger state");
    ESP_RETURN_ON_FALSE(s_trigger_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "trigger service not initialized");

    s_trigger_svc.actions[pin_index][state] = action;
    ESP_LOGI(
        TAG,
        "configured action %s for pin IO%d state=%s",
        (action != NULL) && (action->name != NULL) ? action->name : "NONE",
        (int)pin,
        trigger_svc_state_name(state));
    return ESP_OK;
}

esp_err_t trigger_svc_get_action(
    trigger_svc_pin_t pin,
    trigger_svc_state_t state,
    const trigger_action_t **action)
{
    const size_t pin_index = trigger_svc_pin_to_index(pin);

    ESP_RETURN_ON_FALSE(pin_index < TRIGGER_SVC_PIN_COUNT, ESP_ERR_INVALID_ARG, TAG, "unsupported trigger pin");
    ESP_RETURN_ON_FALSE(trigger_svc_state_is_valid(state), ESP_ERR_INVALID_ARG, TAG, "unsupported trigger state");
    ESP_RETURN_ON_FALSE(action != NULL, ESP_ERR_INVALID_ARG, TAG, "action pointer is null");
    ESP_RETURN_ON_FALSE(s_trigger_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "trigger service not initialized");

    *action = s_trigger_svc.actions[pin_index][state];
    return ESP_OK;
}

esp_err_t trigger_svc_trigger_id_to_pin(uint8_t trigger_id, trigger_svc_pin_t *pin)
{
    ESP_RETURN_ON_FALSE(pin != NULL, ESP_ERR_INVALID_ARG, TAG, "pin pointer is null");

    switch (trigger_id) {
    case 1U:
        *pin = TRIGGER_SVC_PIN_IO5;
        return ESP_OK;
    case 2U:
        *pin = TRIGGER_SVC_PIN_IO4;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}
