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
 * @file trigger_action.h
 * @brief Trigger action interface shared by trigger service implementations.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRIGGER_SVC_PIN_IO4 = 4,
    TRIGGER_SVC_PIN_IO5 = 5,
} trigger_svc_pin_t;

typedef enum {
    TRIGGER_SVC_STATE_LOW = 0,
    TRIGGER_SVC_STATE_HIGH = 1,
} trigger_svc_state_t;

typedef struct {
    trigger_svc_pin_t pin;
    trigger_svc_state_t state;
    uint32_t time_ms;
} trigger_svc_event_t;

typedef struct {
    const char *name;
    esp_err_t (*execute)(const trigger_svc_event_t *event);
} trigger_action_t;

#ifdef __cplusplus
}
#endif
