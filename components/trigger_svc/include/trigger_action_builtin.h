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
 * @file trigger_action_builtin.h
 * @brief Built-in trigger action implementations.
 */

#pragma once

#include <stddef.h>

#include "trigger_action.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for one built-in trigger function token.
 */
#define TRIGGER_ACTION_BUILTIN_FUNCTION_NAME_MAX_LEN 16U

/**
 * @brief Maximum length for one built-in trigger argument payload.
 */
#define TRIGGER_ACTION_BUILTIN_ARGUMENTS_MAX_LEN 16U

/**
 * @brief Resolve one built-in trigger function name and argument list.
 *
 * The current built-ins are:
 * - `OUT_OFF,CH1`
 * - `OUT_ON,CH1`
 * - `OUT_TOGGLE,CH1`
 * - `TIM_START,CH1`
 * - `TIM_PAUSE,CH1`
 *
 * @param function_name Requested function token.
 * @param arguments Raw argument payload after the function token.
 * @param action Output receiving the resolved action descriptor.
 * @param normalized_function_name Optional destination receiving normalized function text.
 * @param normalized_function_name_size Size of @p normalized_function_name in bytes.
 * @param normalized_arguments Optional destination receiving normalized argument text.
 * @param normalized_arguments_size Size of @p normalized_arguments in bytes.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` if the function name or arguments are invalid
 */
esp_err_t trigger_action_builtin_resolve(
    const char *function_name,
    const char *arguments,
    const trigger_action_t **action,
    char *normalized_function_name,
    size_t normalized_function_name_size,
    char *normalized_arguments,
    size_t normalized_arguments_size);

#ifdef __cplusplus
}
#endif
