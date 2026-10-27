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

/** @file scpi_handler_relative.c @brief Relative voltage/current SCPI commands. */
#include "scpi_handler_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "measure_svc.h"

static void scpi_handler_handle_relative(
    scpi_handler_parsed_command_t *parsed, measure_kind_t kind, bool query,
    scpi_handler_write_response_fn_t write_response)
{
    char *argument = parsed->argument;
    measure_channel_t channel;
    uint32_t offset_u4;
    if (argument == NULL) {
        write_response("ERR,\"Expected REL channel[,offset]\"");
        return;
    }
    scpi_handler_trim(argument);
    size_t length = strlen(argument);
    if ((length > 0U) && (argument[length - 1U] == ';')) {
        argument[length - 1U] = '\0';
        scpi_handler_trim(argument);
    }
    char *value = strchr(argument, ',');
    if (!query && (value != NULL)) {
        *value++ = '\0';
        scpi_handler_trim(argument);
        scpi_handler_trim(value);
    }
    if (!scpi_handler_parse_measure_channel(argument, &channel) ||
        (!query && ((value == NULL) || !scpi_handler_parse_value_u4(value, &offset_u4)))) {
        write_response("ERR,\"Expected REL channel[,nonnegative offset]\"");
        return;
    }
    esp_err_t err = query ? measure_svc_get_relative_u4(channel, kind, &offset_u4) :
        measure_svc_set_relative_u4(channel, kind, offset_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, parsed->keyword, err);
    } else if (query) {
        char response[24];
        scpi_handler_format_value_u4(offset_u4, response, sizeof(response));
        write_response(response);
    }
}

bool scpi_handler_handle_relative_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if ((strcmp(keyword, "MEAS:VOLT:REL") == 0) ||
        (strcmp(keyword, "MEAS:VOLT:REL?") == 0) ||
        (strcmp(keyword, "MEAS:CURR:REL") == 0) ||
        (strcmp(keyword, "MEAS:CURR:REL?") == 0)) {
        scpi_handler_handle_relative(parsed,
            strstr(keyword, ":VOLT:") != NULL ? MEASURE_KIND_VOLTAGE : MEASURE_KIND_CURRENT,
            keyword[strlen(keyword) - 1U] == '?', write_response);
        return true;
    }

    return false;
}
