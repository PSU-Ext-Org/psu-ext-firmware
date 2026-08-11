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

#include "scpi_handler_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "output_ctrl.h"
#include "protection_svc.h"

static bool scpi_handler_parse_output_state(const char *argument, bool *enabled)
{
    if ((argument == NULL) || (enabled == NULL)) {
        return false;
    }

    if (strcmp(argument, "0") == 0) {
        *enabled = false;
        return true;
    }

    if (strcmp(argument, "1") == 0) {
        *enabled = true;
        return true;
    }

    return false;
}

static bool scpi_handler_parse_output_set_args(
    char *argument,
    uint8_t *channel,
    bool *enabled)
{
    char *state_arg;
    measure_channel_t parsed_channel;

    if ((argument == NULL) || (channel == NULL) || (enabled == NULL)) {
        return false;
    }

    state_arg = strchr(argument, ',');
    if (state_arg == NULL) {
        return false;
    }

    *state_arg++ = '\0';
    scpi_handler_trim(argument);
    scpi_handler_trim(state_arg);

    if (!scpi_handler_parse_measure_channel(argument, &parsed_channel)) {
        return false;
    }

    if (!scpi_handler_parse_output_state(state_arg, enabled)) {
        return false;
    }

    *channel = (uint8_t)parsed_channel;
    return true;
}

static void scpi_handler_handle_output_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_output_set_args(parsed->argument, &channel, &enabled)) {
        write_response("ERR,\"Expected OUTP <channel>,<0|1>\"");
        return;
    }

    if (channel != PROTECTION_SVC_CHANNEL_CH1) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    if (enabled) {
        err = protection_svc_clear_trips(channel);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "OUTP", err);
            return;
        }

        err = protection_svc_check_ch1_enable_allowed();
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "OUTP", err);
            return;
        }
    }

    err = output_ctrl_set_with_cause(channel, enabled, OUTPUT_CTRL_CHANGE_CAUSE_SCPI);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OUTP", err);
    }
}

static void scpi_handler_handle_output_query(
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    measure_channel_t parsed_channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_measure_channel(parsed->argument, &parsed_channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = output_ctrl_get((uint8_t)parsed_channel, &enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OUTP?", err);
        return;
    }

    write_response(enabled ? "1" : "0");
}

bool scpi_handler_handle_output_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (strcmp(keyword, "OUTP") == 0) {
        scpi_handler_handle_output_set(parsed, write_response);
        return true;
    }

    if (strcmp(keyword, "OUTP?") == 0) {
        scpi_handler_handle_output_query(parsed, write_response);
        return true;
    }

    return false;
}
