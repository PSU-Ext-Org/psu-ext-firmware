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
 * @file output_ctrl.h
 * @brief GPIO-backed output control for PSU output relays.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OUTPUT_CTRL_CHANGE_CAUSE_SCPI = 0,
    OUTPUT_CTRL_CHANGE_CAUSE_TRIGGER,
    OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OVP,
    OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP,
    OUTPUT_CTRL_CHANGE_CAUSE_TIMER_EXPIRY,
    OUTPUT_CTRL_CHANGE_CAUSE_TIMER_CLEAR,
} output_ctrl_change_cause_t;

typedef struct {
    uint8_t channel;
    bool enabled;
    output_ctrl_change_cause_t cause;
} output_ctrl_change_event_t;

typedef void (*output_ctrl_listener_fn_t)(
    const output_ctrl_change_event_t *event,
    void *context);

/**
 * @brief Initialize output control GPIOs.
 *
 * The CH1 relay output is driven low during initialization so output defaults
 * to disabled on every boot.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if GPIO configuration fails
 */
esp_err_t output_ctrl_init(void);

/**
 * @brief Enable or disable one output channel.
 *
 * @param channel Output channel number. Only channel 1 is currently supported.
 * @param enabled `true` to drive the output high, `false` to drive it low.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for unsupported channels
 * - `ESP_ERR_INVALID_STATE` if output control is not initialized
 */
esp_err_t output_ctrl_set(uint8_t channel, bool enabled);

/**
 * @brief Enable or disable one output channel and describe the source.
 *
 * Successful changes notify registered listeners synchronously after the GPIO
 * state has been updated.
 *
 * @param channel Output channel number. Only channel 1 is currently supported.
 * @param enabled `true` to drive the output high, `false` to drive it low.
 * @param cause Why the output state is being applied.
 */
esp_err_t output_ctrl_set_with_cause(
    uint8_t channel,
    bool enabled,
    output_ctrl_change_cause_t cause);

/**
 * @brief Read the cached state of one output channel.
 *
 * @param channel Output channel number. Only channel 1 is currently supported.
 * @param enabled Destination receiving the current state.
 *
 * @return
 * - `ESP_OK` on success
 * - `ESP_ERR_INVALID_ARG` for unsupported channels or a null output pointer
 * - `ESP_ERR_INVALID_STATE` if output control is not initialized
 */
esp_err_t output_ctrl_get(uint8_t channel, bool *enabled);

/**
 * @brief Register a listener for successful output state changes.
 *
 * The same callback/context pair may be registered repeatedly and is treated
 * as already registered.
 */
esp_err_t output_ctrl_register_listener(
    output_ctrl_listener_fn_t listener,
    void *context);

#ifdef __cplusplus
}
#endif
