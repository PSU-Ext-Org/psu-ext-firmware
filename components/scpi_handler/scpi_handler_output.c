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
 * @file scpi_handler_output.c
 * @brief SCPI output relay command handlers.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "output_ctrl.h"
#include "protection_svc.h"

static bool scpi_handler_parse_output_set_args(char *argument, uint8_t *channel, bool *enabled)
{
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    measure_channel_t parsed_channel;
    if (!scpi_handler_arg_channel(&args, &parsed_channel) ||
        !scpi_handler_arg_bool(&args, false, enabled) || !scpi_handler_args_end(&args)) {
        return false;
    }
    *channel = (uint8_t)parsed_channel;
    return true;
}

static void command_outp(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_output_set_args(parsed->argument, &channel, &enabled)) {
        writer->write_text("ERR,\"Expected OUTP <channel>,<0|1>\"");
        return;
    }

    if (channel != PROTECTION_SVC_CHANNEL_CH1) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = protection_svc_set_ch1_output(enabled, OUTPUT_CTRL_CHANGE_CAUSE_SCPI);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OUTP", err);
    }
}

static void command_outp_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    measure_channel_t parsed_channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_measure_channel(parsed->argument, &parsed_channel)) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = output_ctrl_get((uint8_t)parsed_channel, &enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OUTP?", err);
        return;
    }

    scpi_handler_respond_bool(writer->write_text, enabled);
}

static const scpi_handler_command_entry_t s_output_commands[] = {
    {"OUTP", command_outp},
    {"OUTP?", command_outp_query},
};

const scpi_handler_command_module_t scpi_handler_output_module = {
    s_output_commands, sizeof(s_output_commands) / sizeof(s_output_commands[0]),
};
