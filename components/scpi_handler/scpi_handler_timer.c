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

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "timer_svc.h"

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
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    measure_channel_t parsed_channel;
    char *id_arg;
    char *duration_arg;
    if (!scpi_handler_arg_channel(&args, &parsed_channel) ||
        !scpi_handler_arg_string(&args, &id_arg) || strlen(id_arg) != 3U ||
        !scpi_handler_arg_string(&args, &duration_arg) ||
        !scpi_handler_parse_seconds_to_ms(duration_arg, duration_ms) ||
        !scpi_handler_arg_bool(&args, false, enabled) || !scpi_handler_args_end(&args)) {
        return false;
    }
    *channel = (uint8_t)parsed_channel;
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

static void command_tim_add(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    char id[4];
    uint32_t duration_ms;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_timer_add_args(parsed->argument, &channel, id, &duration_ms, &enabled)) {
        writer->write_text("ERR,\"Expected TIMer:ADD CH1,<id>,<seconds>,<0|1>\"");
        return;
    }

    err = timer_svc_add_step(channel, id, duration_ms, enabled);
    if (err == ESP_ERR_NO_MEM) {
        writer->write_text("ERR,\"Timer queue full\"");
        return;
    }

    if (err == ESP_ERR_INVALID_ARG) {
        writer->write_text("ERR,\"Expected unique 3-character timer id\"");
        return;
    }

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "TIM:ADD", err);
    }
}

static void command_tim_clear(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    esp_err_t err;

    if (!scpi_handler_parse_timer_channel(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = timer_svc_clear(channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "TIM:CLE", err);
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

static void command_tim_status_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    timer_svc_status_t status;
    esp_err_t err;

    if (!scpi_handler_parse_timer_channel(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = timer_svc_get_status(channel, &status);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "TIM:STAT?", err);
        return;
    }

    scpi_handler_timer_write_status(&status, writer->write_text);
}

static void command_tim_start(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_timer_toggle(parsed, writer->write_text);
}

static void command_tim_pause(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_timer_toggle(parsed, writer->write_text);
}

static const scpi_handler_command_entry_t s_timer_commands[] = {
    {"[:](TIM|TIMER):ADD", command_tim_add},
    {"[:](TIM|TIMER):(CLEAR|CLE)", command_tim_clear},
    {"[:](TIM|TIMER):(START|STAR)", command_tim_start},
    {"[:](TIM|TIMER):(PAUSE|PAUS)", command_tim_pause},
    {"[:](TIM|TIMER):(STATUS|STAT)?", command_tim_status_query},
};

const scpi_handler_command_module_t scpi_handler_timer_module = {
    s_timer_commands, sizeof(s_timer_commands) / sizeof(s_timer_commands[0]),
};
