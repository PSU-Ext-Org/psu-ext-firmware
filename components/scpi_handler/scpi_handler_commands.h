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

#pragma once

#include <stdbool.h>

#include "scpi_handler.h"
#include "scpi_handler_protocol.h"

typedef void (*scpi_handler_command_callback_t)(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer);
typedef struct {
    const char *pattern;
    scpi_handler_command_callback_t callback;
} scpi_handler_command_entry_t;
typedef struct {
    const scpi_handler_command_entry_t *commands;
    size_t count;
} scpi_handler_command_module_t;

bool scpi_handler_dispatch_registered_command(
    const char *keyword, const char *argument,
    const scpi_handler_response_writer_t *writer);
