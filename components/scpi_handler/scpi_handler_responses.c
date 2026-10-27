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

#include "scpi_handler_responses.h"
#include "scpi_handler_protocol.h"
#include <stdio.h>
#include <inttypes.h>

void scpi_handler_write_esp_error(
    scpi_handler_write_response_fn_t write_response,
    const char *context,
    esp_err_t err)
{
    char response[96];
    snprintf(response, sizeof(response), "ERR,\"%s failed: %s\"", context, esp_err_to_name(err));
    write_response(response);
}

void scpi_handler_format_value_u4(
    uint32_t value_u4,
    char *response,
    size_t response_size)
{
    snprintf(
        response,
        response_size,
        "%" PRIu32 ".%04" PRIu32,
        value_u4 / SCPI_HANDLER_VALUE_SCALE_U4,
        value_u4 % SCPI_HANDLER_VALUE_SCALE_U4);
}

size_t scpi_handler_decimal_digits(size_t value)
{
    size_t digits = 1U;
    while (value >= 10U) {
        value /= 10U;
        digits++;
    }

    return digits;
}

void scpi_handler_write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)((value >> 8) & 0xFFU);
    destination[2] = (uint8_t)((value >> 16) & 0xFFU);
    destination[3] = (uint8_t)((value >> 24) & 0xFFU);
}

void scpi_handler_respond_bool(scpi_handler_write_response_fn_t write_response, bool value)
{
    write_response(value ? "1" : "0");
}

void scpi_handler_respond_integer(scpi_handler_write_response_fn_t write_response, uint32_t value)
{
    char response[16];
    snprintf(response, sizeof(response), "%" PRIu32, value);
    write_response(response);
}

void scpi_handler_respond_value(scpi_handler_write_response_fn_t write_response, uint32_t value)
{
    char response[24];
    scpi_handler_format_value_u4(value, response, sizeof(response));
    write_response(response);
}
