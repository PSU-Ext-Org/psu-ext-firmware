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
 * @file scpi_handler_measure.c
 * @brief Scalar voltage, current and power queries.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *scpi_handler_measure_query_context(measure_kind_t kind)
{
    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        return "MEAS:VOLT?";
    case MEASURE_KIND_CURRENT:
        return "MEAS:CURR?";
    case MEASURE_KIND_POWER:
        return "MEAS:POWER?";
    default:
        return "MEAS?";
    }
}

static void scpi_handler_handle_measure_query(
    const scpi_handler_parsed_command_t *parsed,
    measure_kind_t kind,
    scpi_handler_write_response_fn_t write_response)
{
    measure_channel_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_measure_channel(parsed->argument, &channel)) {
        write_response("ERR,\"Expected measurement channel\"");
        return;
    }

    err = kind == MEASURE_KIND_POWER ?
        measure_svc_read(channel, kind, &value_u4) :
        measure_svc_read_relative(channel, kind, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(
            write_response,
            scpi_handler_measure_query_context(kind),
            err);
        return;
    }

    scpi_handler_respond_value(write_response, value_u4);
}

static void command_meas_volt_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_measure_query(parsed, MEASURE_KIND_VOLTAGE, writer->write_text);
}

static void command_meas_curr_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_measure_query(parsed, MEASURE_KIND_CURRENT, writer->write_text);
}

static void command_meas_power_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_measure_query(parsed, MEASURE_KIND_POWER, writer->write_text);
}

static const scpi_handler_command_entry_t s_measure_commands[] = {
    {"MEAS:VOLT?", command_meas_volt_query},
    {"MEAS:CURR?", command_meas_curr_query},
    {"MEAS:POWER?", command_meas_power_query},
};

const scpi_handler_command_module_t scpi_handler_measure_module = {
    s_measure_commands, sizeof(s_measure_commands) / sizeof(s_measure_commands[0]),
};
