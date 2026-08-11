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
 * @file trigger_config.h
 * @brief Persistent trigger mapping configuration helpers.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "trigger_action_builtin.h"
#include "trigger_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRIGGER_CONFIG_FUNCTION_NAME_MAX_LEN TRIGGER_ACTION_BUILTIN_FUNCTION_NAME_MAX_LEN
#define TRIGGER_CONFIG_ARGUMENTS_MAX_LEN TRIGGER_ACTION_BUILTIN_ARGUMENTS_MAX_LEN

typedef struct {
    uint8_t trigger_id;
    trigger_svc_state_t state;
    char function_name[TRIGGER_CONFIG_FUNCTION_NAME_MAX_LEN];
    char arguments[TRIGGER_CONFIG_ARGUMENTS_MAX_LEN];
} trigger_config_mapping_t;

/**
 * @brief Load persistent trigger mappings and apply defaults when missing.
 *
 * Call this after `trigger_svc_init()`.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if runtime binding or NVS access fails
 */
esp_err_t trigger_config_init(void);

/**
 * @brief Validate, bind, and persist one trigger mapping.
 *
 * @param trigger_id Logical trigger number (`1` or `2`).
 * @param state Debounced trigger state (`LOW` or `HIGH`).
 * @param function_name Function token such as `OUT_ON`.
 * @param arguments Function argument payload such as `CH1`.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for invalid trigger ids, functions, or arguments
 * - `ESP_ERR_INVALID_STATE` if the trigger service/config service is not initialized
 * - Any NVS error if persistence fails; runtime mapping is rolled back in this case
 */
esp_err_t trigger_config_bind(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    const char *function_name,
    const char *arguments);

/**
 * @brief Read the current normalized mapping for one trigger/state slot.
 *
 * The returned mapping comes from the in-memory runtime state after defaults,
 * persisted NVS data, and successful runtime updates have been applied.
 *
 * @param trigger_id Logical trigger number (`1` or `2`).
 * @param state Debounced trigger state (`LOW` or `HIGH`).
 * @param mapping Output receiving the current mapping. Unassigned slots are
 * returned with empty `function_name` and `arguments`.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for invalid trigger ids, states, or null output
 * - `ESP_ERR_INVALID_STATE` if the trigger config service is not initialized
 */
esp_err_t trigger_config_get(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    trigger_config_mapping_t *mapping);

#ifdef __cplusplus
}
#endif
