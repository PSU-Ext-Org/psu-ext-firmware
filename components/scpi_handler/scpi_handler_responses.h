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
#include "scpi_handler.h"
#include "esp_err.h"

void scpi_handler_write_esp_error(
    scpi_handler_write_response_fn_t write_response,
    const char *context,
    esp_err_t err);
void scpi_handler_format_value_u4(
    uint32_t value_u4,
    char *response,
    size_t response_size);
size_t scpi_handler_decimal_digits(size_t value);
void scpi_handler_write_u32_le(uint8_t *destination, uint32_t value);
void scpi_handler_respond_bool(scpi_handler_write_response_fn_t write_response, bool value);
void scpi_handler_respond_integer(scpi_handler_write_response_fn_t write_response, uint32_t value);
void scpi_handler_respond_value(scpi_handler_write_response_fn_t write_response, uint32_t value);
