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

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*scpi_handler_write_response_fn_t)(const char *response);
typedef void (*scpi_handler_write_binary_response_fn_t)(const uint8_t *data, size_t length);

typedef struct {
    scpi_handler_write_response_fn_t write_text;
    scpi_handler_write_binary_response_fn_t write_binary;
} scpi_handler_response_writer_t;

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
