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
 * @file timer_svc.c
 * @brief Volatile queued CH1 timer service for delayed relay transitions.
 */

#include "timer_svc.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "output_ctrl.h"
#include "protection_svc.h"

#define TIMER_SVC_TASK_STACK_WORDS 4096U
#define TIMER_SVC_TASK_PRIORITY 6U

typedef struct {
    char id[4];
    uint32_t duration_ms;
    bool relay_on_after_expiry;
} timer_svc_step_t;

typedef struct {
    bool initialized;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    timer_svc_step_t steps[TIMER_SVC_MAX_STEPS];
    size_t step_count;
    timer_svc_status_kind_t state;
    uint32_t remaining_ms;
    int64_t deadline_us;
} timer_svc_runtime_t;

static const char *TAG = "timer_svc";

static timer_svc_runtime_t s_timer_svc;

static bool timer_svc_channel_is_supported(uint8_t channel)
{
    return channel == TIMER_SVC_CHANNEL_CH1;
}

static uint32_t timer_svc_remaining_from_deadline(int64_t deadline_us, int64_t now_us)
{
    if (deadline_us <= now_us) {
        return 0U;
    }

    const uint64_t delta_us = (uint64_t)(deadline_us - now_us);
    const uint64_t rounded_ms = (delta_us + 999U) / 1000U;
    return rounded_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded_ms;
}

static bool timer_svc_status_is_paused(timer_svc_status_kind_t state)
{
    return (state == TIMER_SVC_STATUS_PAUSED) ||
           (state == TIMER_SVC_STATUS_OVP) ||
           (state == TIMER_SVC_STATUS_OCP) ||
           (state == TIMER_SVC_STATUS_OVR);
}

static void timer_svc_notify_task(void)
{
    if (s_timer_svc.task != NULL) {
        xTaskNotifyGive(s_timer_svc.task);
    }
}

static void timer_svc_shift_queue_left(void)
{
    if (s_timer_svc.step_count == 0U) {
        return;
    }

    for (size_t i = 1U; i < s_timer_svc.step_count; ++i) {
        s_timer_svc.steps[i - 1U] = s_timer_svc.steps[i];
    }

    memset(&s_timer_svc.steps[s_timer_svc.step_count - 1U], 0, sizeof(s_timer_svc.steps[0]));
    s_timer_svc.step_count--;
}

static void timer_svc_start_next_countdown_locked(int64_t now_us)
{
    if (s_timer_svc.step_count == 0U) {
        s_timer_svc.state = TIMER_SVC_STATUS_IDLE;
        s_timer_svc.remaining_ms = 0U;
        s_timer_svc.deadline_us = 0;
        return;
    }

    s_timer_svc.remaining_ms = s_timer_svc.steps[0].duration_ms;
    s_timer_svc.deadline_us = now_us + ((int64_t)s_timer_svc.remaining_ms * 1000LL);
    s_timer_svc.state = TIMER_SVC_STATUS_RUNNING;
}

static void timer_svc_pause_locked(timer_svc_status_kind_t new_state, int64_t now_us)
{
    if ((s_timer_svc.state != TIMER_SVC_STATUS_RUNNING) || (s_timer_svc.step_count == 0U)) {
        return;
    }

    s_timer_svc.remaining_ms = timer_svc_remaining_from_deadline(s_timer_svc.deadline_us, now_us);
    s_timer_svc.deadline_us = 0;
    s_timer_svc.state = new_state;
}

static timer_svc_status_kind_t timer_svc_status_from_output_cause(output_ctrl_change_cause_t cause)
{
    switch (cause) {
    case OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OVP:
        return TIMER_SVC_STATUS_OVP;
    case OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP:
        return TIMER_SVC_STATUS_OCP;
    case OUTPUT_CTRL_CHANGE_CAUSE_SCPI:
    case OUTPUT_CTRL_CHANGE_CAUSE_TRIGGER:
    default:
        return TIMER_SVC_STATUS_OVR;
    }
}

static void timer_svc_output_listener(
    const output_ctrl_change_event_t *event,
    void *context)
{
    (void)context;

    if ((event == NULL) || !timer_svc_channel_is_supported(event->channel) || (s_timer_svc.lock == NULL)) {
        return;
    }

    if ((event->cause == OUTPUT_CTRL_CHANGE_CAUSE_TIMER_EXPIRY) ||
        (event->cause == OUTPUT_CTRL_CHANGE_CAUSE_TIMER_CLEAR)) {
        return;
    }

    if (xSemaphoreTake(s_timer_svc.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if ((s_timer_svc.state == TIMER_SVC_STATUS_RUNNING) && (s_timer_svc.step_count > 0U)) {
        timer_svc_pause_locked(
            timer_svc_status_from_output_cause(event->cause),
            esp_timer_get_time());
        xSemaphoreGive(s_timer_svc.lock);
        timer_svc_notify_task();
        return;
    }

    xSemaphoreGive(s_timer_svc.lock);
}

static esp_err_t timer_svc_take_lock(void)
{
    ESP_RETURN_ON_FALSE(s_timer_svc.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "timer lock not initialized");
    return xSemaphoreTake(s_timer_svc.lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void timer_svc_complete_due_steps(void)
{
    while (true) {
        timer_svc_step_t expired_step;
        bool should_apply = false;
        int64_t now_us = esp_timer_get_time();

        if (timer_svc_take_lock() != ESP_OK) {
            return;
        }

        if ((s_timer_svc.state != TIMER_SVC_STATUS_RUNNING) ||
            (s_timer_svc.step_count == 0U) ||
            (s_timer_svc.deadline_us > now_us)) {
            xSemaphoreGive(s_timer_svc.lock);
            return;
        }

        expired_step = s_timer_svc.steps[0];
        timer_svc_shift_queue_left();
        should_apply = true;
        timer_svc_start_next_countdown_locked(now_us);
        xSemaphoreGive(s_timer_svc.lock);

        if (should_apply) {
            esp_err_t err = output_ctrl_set_with_cause(
                TIMER_SVC_CHANNEL_CH1,
                expired_step.relay_on_after_expiry,
                OUTPUT_CTRL_CHANGE_CAUSE_TIMER_EXPIRY);
            if (err != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "applying timer step %s relay state failed: %s",
                    expired_step.id,
                    esp_err_to_name(err));
            } else {
                ESP_LOGI(
                    TAG,
                    "timer step %s expired -> relay %s",
                    expired_step.id,
                    expired_step.relay_on_after_expiry ? "ON" : "OFF");
            }
        }
    }
}

static void timer_svc_task(void *arg)
{
    (void)arg;

    while (true) {
        TickType_t wait_ticks = portMAX_DELAY;

        if (timer_svc_take_lock() == ESP_OK) {
            if ((s_timer_svc.state == TIMER_SVC_STATUS_RUNNING) && (s_timer_svc.step_count > 0U)) {
                const uint32_t remaining_ms = timer_svc_remaining_from_deadline(
                    s_timer_svc.deadline_us,
                    esp_timer_get_time());
                wait_ticks = pdMS_TO_TICKS(remaining_ms == 0U ? 1U : remaining_ms);
                if (wait_ticks == 0U) {
                    wait_ticks = 1U;
                }
            }
            xSemaphoreGive(s_timer_svc.lock);
        }

        ulTaskNotifyTake(pdTRUE, wait_ticks);
        timer_svc_complete_due_steps();
    }
}

esp_err_t timer_svc_init(void)
{
    if (s_timer_svc.initialized) {
        return ESP_OK;
    }

    if (s_timer_svc.lock == NULL) {
        s_timer_svc.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_timer_svc.lock != NULL, ESP_ERR_NO_MEM, TAG, "timer lock alloc failed");
    }

    ESP_RETURN_ON_ERROR(
        output_ctrl_register_listener(timer_svc_output_listener, NULL),
        TAG,
        "registering output listener failed");

    if (s_timer_svc.task == NULL) {
        const BaseType_t created = xTaskCreate(
            timer_svc_task,
            "timer_svc",
            TIMER_SVC_TASK_STACK_WORDS,
            NULL,
            TIMER_SVC_TASK_PRIORITY,
            &s_timer_svc.task);
        if (created != pdPASS) {
            s_timer_svc.task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    s_timer_svc.state = TIMER_SVC_STATUS_IDLE;
    s_timer_svc.remaining_ms = 0U;
    s_timer_svc.deadline_us = 0;
    s_timer_svc.initialized = true;
    ESP_LOGI(TAG, "timer service initialized");
    return ESP_OK;
}

esp_err_t timer_svc_add_step(
    uint8_t channel,
    const char id[4],
    uint32_t duration_ms,
    bool relay_on_after_expiry)
{
    ESP_RETURN_ON_FALSE(timer_svc_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported timer channel");
    ESP_RETURN_ON_FALSE(id != NULL, ESP_ERR_INVALID_ARG, TAG, "timer id is null");
    ESP_RETURN_ON_FALSE(s_timer_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "timer service not initialized");

    ESP_RETURN_ON_ERROR(timer_svc_take_lock(), TAG, "taking timer lock failed");

    if (s_timer_svc.step_count >= TIMER_SVC_MAX_STEPS) {
        xSemaphoreGive(s_timer_svc.lock);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0U; i < s_timer_svc.step_count; ++i) {
        if (strcmp(s_timer_svc.steps[i].id, id) == 0) {
            xSemaphoreGive(s_timer_svc.lock);
            return ESP_ERR_INVALID_ARG;
        }
    }

    strlcpy(s_timer_svc.steps[s_timer_svc.step_count].id, id, sizeof(s_timer_svc.steps[0].id));
    s_timer_svc.steps[s_timer_svc.step_count].duration_ms = duration_ms;
    s_timer_svc.steps[s_timer_svc.step_count].relay_on_after_expiry = relay_on_after_expiry;
    s_timer_svc.step_count++;
    xSemaphoreGive(s_timer_svc.lock);

    timer_svc_notify_task();
    return ESP_OK;
}

esp_err_t timer_svc_clear(uint8_t channel)
{
    ESP_RETURN_ON_FALSE(timer_svc_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported timer channel");
    ESP_RETURN_ON_FALSE(s_timer_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "timer service not initialized");

    ESP_RETURN_ON_ERROR(timer_svc_take_lock(), TAG, "taking timer lock failed");
    memset(s_timer_svc.steps, 0, sizeof(s_timer_svc.steps));
    s_timer_svc.step_count = 0U;
    s_timer_svc.state = TIMER_SVC_STATUS_IDLE;
    s_timer_svc.remaining_ms = 0U;
    s_timer_svc.deadline_us = 0;
    xSemaphoreGive(s_timer_svc.lock);

    timer_svc_notify_task();
    return output_ctrl_set_with_cause(
        channel,
        false,
        OUTPUT_CTRL_CHANGE_CAUSE_TIMER_CLEAR);
}

esp_err_t timer_svc_toggle_run_pause(uint8_t channel)
{
    ESP_RETURN_ON_FALSE(timer_svc_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported timer channel");
    ESP_RETURN_ON_FALSE(s_timer_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "timer service not initialized");

    ESP_RETURN_ON_ERROR(timer_svc_take_lock(), TAG, "taking timer lock failed");

    if (s_timer_svc.step_count == 0U) {
        xSemaphoreGive(s_timer_svc.lock);
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_timer_svc.state == TIMER_SVC_STATUS_RUNNING) {
        timer_svc_pause_locked(TIMER_SVC_STATUS_PAUSED, now_us);
    } else if ((s_timer_svc.state == TIMER_SVC_STATUS_IDLE) || timer_svc_status_is_paused(s_timer_svc.state)) {
        if (s_timer_svc.state == TIMER_SVC_STATUS_IDLE) {
            s_timer_svc.remaining_ms = s_timer_svc.steps[0].duration_ms;
        }

        if ((s_timer_svc.state == TIMER_SVC_STATUS_OVP) || (s_timer_svc.state == TIMER_SVC_STATUS_OCP)) {
            const esp_err_t clear_err = protection_svc_clear_trips(channel);
            if (clear_err != ESP_OK) {
                xSemaphoreGive(s_timer_svc.lock);
                return clear_err;
            }
        }

        s_timer_svc.deadline_us = now_us + ((int64_t)s_timer_svc.remaining_ms * 1000LL);
        s_timer_svc.state = TIMER_SVC_STATUS_RUNNING;
    } else {
        xSemaphoreGive(s_timer_svc.lock);
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreGive(s_timer_svc.lock);
    timer_svc_notify_task();
    return ESP_OK;
}

esp_err_t timer_svc_get_status(uint8_t channel, timer_svc_status_t *status)
{
    ESP_RETURN_ON_FALSE(timer_svc_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported timer channel");
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status pointer is null");
    ESP_RETURN_ON_FALSE(s_timer_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "timer service not initialized");

    ESP_RETURN_ON_ERROR(timer_svc_take_lock(), TAG, "taking timer lock failed");

    memset(status, 0, sizeof(*status));
    status->state = s_timer_svc.state;

    if (s_timer_svc.step_count == 0U) {
        strlcpy(status->id, "NONE", sizeof(status->id));
        status->state = TIMER_SVC_STATUS_IDLE;
        status->remaining_ms = 0U;
        xSemaphoreGive(s_timer_svc.lock);
        return ESP_OK;
    }

    strlcpy(status->id, s_timer_svc.steps[0].id, sizeof(status->id));
    if (s_timer_svc.state == TIMER_SVC_STATUS_RUNNING) {
        status->remaining_ms = timer_svc_remaining_from_deadline(
            s_timer_svc.deadline_us,
            esp_timer_get_time());
    } else if (s_timer_svc.state == TIMER_SVC_STATUS_IDLE) {
        status->remaining_ms = s_timer_svc.steps[0].duration_ms;
    } else {
        status->remaining_ms = s_timer_svc.remaining_ms;
    }

    xSemaphoreGive(s_timer_svc.lock);
    return ESP_OK;
}

const char *timer_svc_status_name(timer_svc_status_kind_t status)
{
    switch (status) {
    case TIMER_SVC_STATUS_IDLE:
        return "IDLE";
    case TIMER_SVC_STATUS_RUNNING:
        return "RUNNING";
    case TIMER_SVC_STATUS_PAUSED:
        return "PAUSED";
    case TIMER_SVC_STATUS_OVP:
        return "OVP";
    case TIMER_SVC_STATUS_OCP:
        return "OCP";
    case TIMER_SVC_STATUS_OVR:
        return "OVR";
    default:
        return "UNKNOWN";
    }
}
