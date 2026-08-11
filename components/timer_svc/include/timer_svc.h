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
 * @file timer_svc.h
 * @brief Volatile queued CH1 timer service for delayed relay transitions.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMER_SVC_CHANNEL_CH1 1U
#define TIMER_SVC_MAX_STEPS 10U

typedef enum {
    TIMER_SVC_STATUS_IDLE = 0,
    TIMER_SVC_STATUS_RUNNING,
    TIMER_SVC_STATUS_PAUSED,
    TIMER_SVC_STATUS_OVP,
    TIMER_SVC_STATUS_OCP,
    TIMER_SVC_STATUS_OVR,
} timer_svc_status_kind_t;

typedef struct {
    char id[5];
    timer_svc_status_kind_t state;
    uint32_t remaining_ms;
} timer_svc_status_t;

esp_err_t timer_svc_init(void);

esp_err_t timer_svc_add_step(
    uint8_t channel,
    const char id[4],
    uint32_t duration_ms,
    bool relay_on_after_expiry);

esp_err_t timer_svc_clear(uint8_t channel);

esp_err_t timer_svc_toggle_run_pause(uint8_t channel);

esp_err_t timer_svc_get_status(uint8_t channel, timer_svc_status_t *status);

const char *timer_svc_status_name(timer_svc_status_kind_t status);

#ifdef __cplusplus
}
#endif
