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
 * @file scpi_handler_system.c
 * @brief SCPI system command handlers.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "timebase_svc.h"
#include "scpi_handler_datetime.h"

static const char *SCPI_HANDLER_DATETIME_CONTEXT = "SYST:DATETIME";
static const char *SCPI_HANDLER_DATETIME_QUERY_CONTEXT = "SYST:DATETIME?";
static const char *SCPI_HANDLER_DATETIME_MAP_CONTEXT = "SYST:DATETIME:MAP?";
static void command_syst_datetime(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    struct tm tm_value;
    uint16_t millisecond;
    uint64_t unix_ms;
    esp_err_t err;

    if ((parsed->argument == NULL) || !scpi_handler_parse_datetime_arg(parsed->argument, &tm_value, &millisecond)) {
        writer->write_text("ERR,\"Expected SYST:DATETIME YYYY-MM-DD HH:MM:SS.sss\"");
        return;
    }

    if (!scpi_handler_datetime_to_unix_ms(&tm_value, millisecond, &unix_ms)) {
        writer->write_text("ERR,\"Datetime must be on or after 1970-01-01 00:00:00.000\"");
        return;
    }

    err = timebase_svc_set_base_unix_ms(unix_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, SCPI_HANDLER_DATETIME_CONTEXT, err);
    }
}

static void scpi_handler_handle_system_datetime_query(scpi_handler_write_response_fn_t write_response)
{
    struct tm tm_value;
    char response[32];
    uint64_t unix_ms;
    uint16_t millisecond;
    esp_err_t err;

    err = timebase_svc_get_current_unix_ms(&unix_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, SCPI_HANDLER_DATETIME_QUERY_CONTEXT, err);
        return;
    }

    if (!scpi_handler_unix_ms_to_datetime(unix_ms, &tm_value, &millisecond)) {
        write_response("ERR,\"Failed to convert current datetime\"");
        return;
    }

    if (!scpi_handler_format_datetime(&tm_value, millisecond, response, sizeof(response))) {
        write_response("ERR,\"Failed to format current datetime\"");
        return;
    }

    write_response(response);
}

static void scpi_handler_handle_system_datetime_map_query(scpi_handler_write_response_fn_t write_response)
{
    struct tm tm_value;
    timebase_svc_mapping_t mapping;
    char timestamp[32];
    char response[48];
    uint16_t millisecond;
    esp_err_t err;

    err = timebase_svc_get_base_mapping(&mapping);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, SCPI_HANDLER_DATETIME_MAP_CONTEXT, err);
        return;
    }

    if (!scpi_handler_unix_ms_to_datetime(mapping.unix_ms, &tm_value, &millisecond)) {
        write_response("ERR,\"Failed to convert datetime mapping\"");
        return;
    }

    if (!scpi_handler_format_datetime(&tm_value, millisecond, timestamp, sizeof(timestamp))) {
        write_response("ERR,\"Failed to format datetime mapping\"");
        return;
    }

    snprintf(response, sizeof(response), "%s,%" PRIu32, timestamp, mapping.uptime_ms);
    write_response(response);
}

static void command_syst_err_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    writer->write_text("0,\"No error\"");
}

static void command_syst_datetime_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_system_datetime_query(writer->write_text);
}

static void command_syst_datetime_map_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_system_datetime_map_query(writer->write_text);
}

static void command_idn_query(scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    writer->write_text("PSU-EXT,ESP32-S3,0001,0.1.0");
}

static const scpi_handler_command_entry_t s_system_commands[] = {
    {"*IDN?", command_idn_query},
    {"SYST:ERR?", command_syst_err_query},
    {"SYSTEM:ERROR?", command_syst_err_query},
    {":SYST:ERR?", command_syst_err_query},
    {":SYSTEM:ERROR?", command_syst_err_query},
    {"SYST:DATETIME", command_syst_datetime},
    {"SYSTEM:DATETIME", command_syst_datetime},
    {":SYST:DATETIME", command_syst_datetime},
    {":SYSTEM:DATETIME", command_syst_datetime},
    {"SYST:DATETIME?", command_syst_datetime_query},
    {"SYSTEM:DATETIME?", command_syst_datetime_query},
    {":SYST:DATETIME?", command_syst_datetime_query},
    {":SYSTEM:DATETIME?", command_syst_datetime_query},
    {"SYST:DATETIME:MAP?", command_syst_datetime_map_query},
    {"SYSTEM:DATETIME:MAP?", command_syst_datetime_map_query},
    {":SYST:DATETIME:MAP?", command_syst_datetime_map_query},
    {":SYSTEM:DATETIME:MAP?", command_syst_datetime_map_query},
};

const scpi_handler_command_module_t scpi_handler_system_module = {
    s_system_commands, sizeof(s_system_commands) / sizeof(s_system_commands[0]),
};
