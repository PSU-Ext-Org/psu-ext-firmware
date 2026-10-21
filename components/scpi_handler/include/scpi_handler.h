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
 * @file scpi_handler.h
 * @brief Shared SCPI command parsing and execution helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*scpi_handler_write_response_fn_t)(const char *response);
typedef void (*scpi_handler_write_binary_response_fn_t)(const uint8_t *data, size_t length);

typedef struct {
    scpi_handler_write_response_fn_t write_text;
    scpi_handler_write_binary_response_fn_t write_binary;
} scpi_handler_response_writer_t;

/** Callback arguments and writer are valid only for the duration of the call.
 * argument is NULL when absent; its case is preserved and whitespace trimmed.
 * Callbacks may run concurrently on different transport tasks.
 */
typedef void (*scpi_handler_command_fn_t)(
    const char *argument,
    const scpi_handler_response_writer_t *writer,
    void *context);

typedef struct {
    const char *keyword;
    scpi_handler_command_fn_t callback;
    void *context;
} scpi_handler_command_t;

enum { SCPI_HANDLER_MAX_REGISTERED_COMMANDS = 32 };

/** Register an entire command table atomically, without heap allocation.
 * Keywords must be uppercase literal command names (including '?' for queries).
 * Register short and long spellings separately; pattern expansion is not used.
 * Entries are copied; keyword strings must stay immutable until reboot, and
 * callback context must remain valid until reboot. Keywords are at most 127 bytes.
 * Registration is safe alongside dispatch; callbacks run outside the registry
 * lock. Built-in commands take priority and cannot be overridden.
 * Returns ESP_ERR_INVALID_ARG for invalid entries or duplicate table keywords,
 * ESP_ERR_INVALID_STATE for an already registered keyword, and ESP_ERR_NO_MEM
 * when the fixed registry is full. Failures leave the registry unchanged.
 * There is no unregister operation; register once during module startup.
 */
esp_err_t scpi_handler_register_commands(
    const scpi_handler_command_t *commands,
    size_t count);

/**
 * @brief Decode and execute one mutable SCPI command line.
 *
 * The command buffer is normalized in place during parsing so transports can
 * share the same handler without copying into an additional parse buffer.
 *
 * @param command Mutable null-terminated command string.
 * @param writer Transport callbacks used to send text or binary responses.
 */
void scpi_handler_handle_command(
    char *command,
    const scpi_handler_response_writer_t *writer);

#ifdef __cplusplus
}
#endif
