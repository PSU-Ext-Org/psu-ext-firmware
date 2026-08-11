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
 * @file scpi_handler_timer.c
 * @brief SCPI timer queue command handlers.
 */

#include "scpi_handler_internal.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "timer_svc.h"

static bool scpi_handler_timer_keyword_matches(const char *keyword, const char *short_form, const char *long_form)
{
    if ((strcmp(keyword, short_form) == 0) || (strcmp(keyword, long_form) == 0)) {
        return true;
    }

    if ((keyword[0] == ':') && (strcmp(keyword + 1, short_form) == 0)) {
        return true;
    }

    if ((keyword[0] == ':') && (strcmp(keyword + 1, long_form) == 0)) {
        return true;
    }

    return false;
}

static bool scpi_handler_parse_timer_state(const char *argument, bool *enabled)
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

static bool scpi_handler_parse_timer_channel(const char *argument, uint8_t *channel)
{
    measure_channel_t parsed_channel;

    if ((argument == NULL) || (channel == NULL)) {
        return false;
    }

    if (!scpi_handler_parse_measure_channel(argument, &parsed_channel)) {
        return false;
    }

    *channel = (uint8_t)parsed_channel;
    return true;
}

static bool scpi_handler_parse_timer_add_args(
    char *argument,
    uint8_t *channel,
    char id[4],
    uint32_t *duration_ms,
    bool *enabled)
{
    char *id_arg;
    char *duration_arg;
    char *state_arg;

    if ((argument == NULL) || (channel == NULL) || (id == NULL) || (duration_ms == NULL) || (enabled == NULL)) {
        return false;
    }

    id_arg = strchr(argument, ',');
    if (id_arg == NULL) {
        return false;
    }
    *id_arg++ = '\0';

    duration_arg = strchr(id_arg, ',');
    if (duration_arg == NULL) {
        return false;
    }
    *duration_arg++ = '\0';

    state_arg = strchr(duration_arg, ',');
    if (state_arg == NULL) {
        return false;
    }
    *state_arg++ = '\0';

    scpi_handler_trim(argument);
    scpi_handler_trim(id_arg);
    scpi_handler_trim(duration_arg);
    scpi_handler_trim(state_arg);

    if ((strchr(state_arg, ',') != NULL) ||
        !scpi_handler_parse_timer_channel(argument, channel) ||
        (strlen(id_arg) != 3U) ||
        !scpi_handler_parse_seconds_to_ms(duration_arg, duration_ms) ||
        !scpi_handler_parse_timer_state(state_arg, enabled)) {
        return false;
    }

    memcpy(id, id_arg, 3U);
    id[3] = '\0';
    return true;
}

static void scpi_handler_timer_write_status(
    const timer_svc_status_t *status,
    scpi_handler_write_response_fn_t write_response)
{
    char response[48];

    snprintf(
        response,
        sizeof(response),
        "%s,%s,%" PRIu32 ".%03" PRIu32,
        status->id,
        timer_svc_status_name(status->state),
        status->remaining_ms / 1000U,
        status->remaining_ms % 1000U);
    write_response(response);
}

static void scpi_handler_handle_timer_add(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    char id[4];
    uint32_t duration_ms;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_timer_add_args(parsed->argument, &channel, id, &duration_ms, &enabled)) {
        write_response("ERR,\"Expected TIMer:ADD CH1,<id>,<seconds>,<0|1>\"");
        return;
    }

    err = timer_svc_add_step(channel, id, duration_ms, enabled);
    if (err == ESP_ERR_NO_MEM) {
        write_response("ERR,\"Timer queue full\"");
        return;
    }

    if (err == ESP_ERR_INVALID_ARG) {
        write_response("ERR,\"Expected unique 3-character timer id\"");
        return;
    }

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TIM:ADD", err);
    }
}

static void scpi_handler_handle_timer_clear(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    esp_err_t err;

    if (!scpi_handler_parse_timer_channel(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = timer_svc_clear(channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TIM:CLE", err);
    }
}

static void scpi_handler_handle_timer_toggle(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    esp_err_t err;

    if (!scpi_handler_parse_timer_channel(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = timer_svc_toggle_run_pause(channel);
    if (err == ESP_ERR_INVALID_STATE) {
        write_response("ERR,\"No timer queued\"");
        return;
    }

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TIM:RUN", err);
    }
}

static void scpi_handler_handle_timer_status(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    timer_svc_status_t status;
    esp_err_t err;

    if (!scpi_handler_parse_timer_channel(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = timer_svc_get_status(channel, &status);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TIM:STAT?", err);
        return;
    }

    scpi_handler_timer_write_status(&status, write_response);
}

bool scpi_handler_handle_timer_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (scpi_handler_timer_keyword_matches(keyword, "TIM:ADD", "TIMER:ADD")) {
        scpi_handler_handle_timer_add(parsed, write_response);
        return true;
    }

    if (scpi_handler_timer_keyword_matches(keyword, "TIM:CLEAR", "TIMER:CLEAR") ||
        scpi_handler_timer_keyword_matches(keyword, "TIM:CLE", "TIMER:CLE")) {
        scpi_handler_handle_timer_clear(parsed, write_response);
        return true;
    }

    if (scpi_handler_timer_keyword_matches(keyword, "TIM:START", "TIMER:START") ||
        scpi_handler_timer_keyword_matches(keyword, "TIM:STAR", "TIMER:STAR")) {
        scpi_handler_handle_timer_toggle(parsed, write_response);
        return true;
    }

    if (scpi_handler_timer_keyword_matches(keyword, "TIM:PAUSE", "TIMER:PAUSE") ||
        scpi_handler_timer_keyword_matches(keyword, "TIM:PAUS", "TIMER:PAUS")) {
        scpi_handler_handle_timer_toggle(parsed, write_response);
        return true;
    }

    if (scpi_handler_timer_keyword_matches(keyword, "TIM:STATUS?", "TIMER:STATUS?") ||
        scpi_handler_timer_keyword_matches(keyword, "TIM:STAT?", "TIMER:STAT?")) {
        scpi_handler_handle_timer_status(parsed, write_response);
        return true;
    }

    return false;
}
