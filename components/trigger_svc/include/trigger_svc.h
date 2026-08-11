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
 * @file trigger_svc.h
 * @brief Debounced external trigger service for GPIO-backed actions.
 */

#pragma once

#include "trigger_action.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize IO4/IO5 trigger inputs, debounce worker, and ISR handlers.
 */
esp_err_t trigger_svc_init(void);

/**
 * @brief Select the action implementation for one pin/state combination.
 */
esp_err_t trigger_svc_set_action(
    trigger_svc_pin_t pin,
    trigger_svc_state_t state,
    const trigger_action_t *action);

/**
 * @brief Read the current action implementation for one pin/state combination.
 */
esp_err_t trigger_svc_get_action(
    trigger_svc_pin_t pin,
    trigger_svc_state_t state,
    const trigger_action_t **action);

/**
 * @brief Map one logical trigger identifier to its physical input pin enum.
 *
 * Public SCPI commands use trigger ids `1` and `2`.
 *
 * @param trigger_id Logical trigger id.
 * @param pin Output receiving the matching trigger pin enum.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if the id is unsupported or @p pin is NULL
 */
esp_err_t trigger_svc_trigger_id_to_pin(uint8_t trigger_id, trigger_svc_pin_t *pin);

#ifdef __cplusplus
}
#endif
