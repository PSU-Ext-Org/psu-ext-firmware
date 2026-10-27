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
#include <stddef.h>
#include <stdint.h>
#include "measure_types.h"
#include "scpi_handler_protocol.h"

bool scpi_handler_parse_measure_channel(
    const char *argument,
    measure_channel_t *channel);
bool scpi_handler_parse_size_arg(const char *argument, size_t *value);
bool scpi_handler_parse_value_u4(const char *argument, uint32_t *value_u4);
bool scpi_handler_parse_seconds_to_ms(const char *argument, uint32_t *value_ms);

/* Reader consumes comma-separated tokens in place. Empty/trailing tokens fail.
 * A trailing semicolon is accepted only when explicitly requested (REL). */
typedef struct {
    char *next;
    bool failed;
} scpi_handler_args_t;
scpi_handler_args_t scpi_handler_args(char *text, bool allow_semicolon);
bool scpi_handler_arg_string(scpi_handler_args_t *args, char **value);
bool scpi_handler_arg_channel(scpi_handler_args_t *args, measure_channel_t *value);
bool scpi_handler_arg_value(scpi_handler_args_t *args, uint32_t *value);
bool scpi_handler_arg_bool(scpi_handler_args_t *args, bool allow_words, bool *value);
bool scpi_handler_args_end(const scpi_handler_args_t *args);
