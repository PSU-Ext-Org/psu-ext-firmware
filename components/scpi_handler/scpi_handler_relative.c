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
#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

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
    scpi_handler_args_t args = scpi_handler_args(argument, true);
    if (!scpi_handler_arg_channel(&args, &channel) ||
        (!query && !scpi_handler_arg_value(&args, &offset_u4)) ||
        !scpi_handler_args_end(&args)) {
        write_response("ERR,\"Expected REL channel[,nonnegative offset]\"");
        return;
    }
    esp_err_t err = query ? measure_svc_get_relative_u4(channel, kind, &offset_u4) :
        measure_svc_set_relative_u4(channel, kind, offset_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, parsed->keyword, err);
    } else if (query) {
        scpi_handler_respond_value(write_response, offset_u4);
    }
}

static void command_meas_volt_rel(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_relative(parsed, MEASURE_KIND_VOLTAGE, false, writer->write_text);
}

static void command_meas_volt_rel_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_relative(parsed, MEASURE_KIND_VOLTAGE, true, writer->write_text);
}

static void command_meas_curr_rel(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_relative(parsed, MEASURE_KIND_CURRENT, false, writer->write_text);
}

static void command_meas_curr_rel_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_relative(parsed, MEASURE_KIND_CURRENT, true, writer->write_text);
}

static const scpi_handler_command_entry_t s_relative_commands[] = {
    {"MEAS:VOLT:REL", command_meas_volt_rel},
    {"MEAS:VOLT:REL?", command_meas_volt_rel_query},
    {"MEAS:CURR:REL", command_meas_curr_rel},
    {"MEAS:CURR:REL?", command_meas_curr_rel_query},
};

const scpi_handler_command_module_t scpi_handler_relative_module = {
    s_relative_commands, sizeof(s_relative_commands) / sizeof(s_relative_commands[0]),
};
