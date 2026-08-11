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
 * @file scpi_handler_protection.c
 * @brief SCPI protection command handlers.
 */

#include "scpi_handler_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protection_svc.h"

static bool scpi_handler_keyword_matches(const char *keyword, const char *short_form, const char *source_form)
{
    if ((strcmp(keyword, short_form) == 0) || (strcmp(keyword, source_form) == 0)) {
        return true;
    }

    if ((strncmp(keyword, ":SOUR:", 6U) == 0) && (strncmp(source_form, ":SOURCE:", 8U) == 0)) {
        return strcmp(keyword + 6U, source_form + 8U) == 0;
    }

    if ((strncmp(keyword, "SOURCE:", 7U) == 0) && (strncmp(source_form, ":SOURCE:", 8U) == 0)) {
        return strcmp(keyword + 7U, source_form + 8U) == 0;
    }

    if ((strncmp(keyword, "SOUR:", 5U) == 0) && (strncmp(source_form, ":SOURCE:", 8U) == 0)) {
        return strcmp(keyword + 5U, source_form + 8U) == 0;
    }

    return false;
}

static bool scpi_handler_parse_bool_state(const char *argument, bool *enabled)
{
    char state_upper[8];

    if ((argument == NULL) || (enabled == NULL)) {
        return false;
    }

    strlcpy(state_upper, argument, sizeof(state_upper));
    scpi_handler_uppercase(state_upper);

    if ((strcmp(state_upper, "1") == 0) || (strcmp(state_upper, "ON") == 0)) {
        *enabled = true;
        return true;
    }

    if ((strcmp(state_upper, "0") == 0) || (strcmp(state_upper, "OFF") == 0)) {
        *enabled = false;
        return true;
    }

    return false;
}

static bool scpi_handler_parse_channel_value_args(
    char *argument,
    uint8_t *channel,
    uint32_t *value_u4)
{
    char *value_arg;
    measure_channel_t parsed_channel;

    if ((argument == NULL) || (channel == NULL) || (value_u4 == NULL)) {
        return false;
    }

    value_arg = strchr(argument, ',');
    if (value_arg == NULL) {
        return false;
    }

    *value_arg++ = '\0';
    scpi_handler_trim(argument);
    scpi_handler_trim(value_arg);

    if (!scpi_handler_parse_measure_channel(argument, &parsed_channel)) {
        return false;
    }

    if (!scpi_handler_parse_value_u4(value_arg, value_u4)) {
        return false;
    }

    *channel = (uint8_t)parsed_channel;
    return true;
}

static bool scpi_handler_parse_channel_bool_args(
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

    if (!scpi_handler_parse_bool_state(state_arg, enabled)) {
        return false;
    }

    *channel = (uint8_t)parsed_channel;
    return true;
}

static bool scpi_handler_parse_channel_arg(const char *argument, uint8_t *channel)
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

static void scpi_handler_write_value_response(
    uint32_t value_u4,
    scpi_handler_write_response_fn_t write_response)
{
    char response[24];
    scpi_handler_format_value_u4(value_u4, response, sizeof(response));
    write_response(response);
}

static void scpi_handler_handle_ovp_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_value_args(parsed->argument, &channel, &value_u4)) {
        write_response("ERR,\"Expected OVP CH0|CH1,<value>\"");
        return;
    }

    err = protection_svc_set_ovp_u4(channel, value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OVP", err);
    }
}

static void scpi_handler_handle_ovp_query(
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 0|CH0 or 1|CH1\"");
        return;
    }

    err = protection_svc_get_ovp_u4(channel, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OVP?", err);
        return;
    }

    scpi_handler_write_value_response(value_u4, write_response);
}

static void scpi_handler_handle_ocp_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_value_args(parsed->argument, &channel, &value_u4)) {
        write_response("ERR,\"Expected OCP CH1,<value>\"");
        return;
    }

    err = protection_svc_set_ocp_u4(channel, value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OCP", err);
    }
}

static void scpi_handler_handle_ocp_query(
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = protection_svc_get_ocp_u4(channel, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OCP?", err);
        return;
    }

    scpi_handler_write_value_response(value_u4, write_response);
}

static void scpi_handler_handle_ocp_state_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_channel_bool_args(parsed->argument, &channel, &enabled)) {
        write_response("ERR,\"Expected OCP:STATe CH1,<0|1|OFF|ON>\"");
        return;
    }

    err = protection_svc_set_ocp_enabled(channel, enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OCP:STAT", err);
    }
}

static void scpi_handler_handle_ocp_state_query(
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = protection_svc_get_ocp_enabled(channel, &enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "OCP:STAT?", err);
        return;
    }

    write_response(enabled ? "1" : "0");
}

static void scpi_handler_handle_trip_query(
    const scpi_handler_parsed_command_t *parsed,
    bool ovp,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    bool tripped;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response(ovp ? "ERR,\"Expected channel 0|CH0 or 1|CH1\"" : "ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = ovp ?
        protection_svc_get_ovp_tripped(channel, &tripped) :
        protection_svc_get_ocp_tripped(channel, &tripped);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, ovp ? "OVP:PROT:STAT?" : "OCP:PROT:STAT?", err);
        return;
    }

    write_response(tripped ? "1" : "0");
}

static void scpi_handler_handle_reset_protection(
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response("ERR,\"Expected channel 0|CH0 or 1|CH1\"");
        return;
    }

    err = protection_svc_clear_trips(channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "RESET:PROT", err);
    }
}

bool scpi_handler_handle_protection_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (scpi_handler_keyword_matches(keyword, "OVP", ":SOURCE:OVP")) {
        scpi_handler_handle_ovp_set(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OVP?", ":SOURCE:OVP?")) {
        scpi_handler_handle_ovp_query(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OCP", ":SOURCE:OCP")) {
        scpi_handler_handle_ocp_set(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OCP?", ":SOURCE:OCP?")) {
        scpi_handler_handle_ocp_query(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OCP:STATE", ":SOURCE:OCP:STATE") ||
        scpi_handler_keyword_matches(keyword, "OCP:STAT", ":SOURCE:OCP:STAT")) {
        scpi_handler_handle_ocp_state_set(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OCP:STATE?", ":SOURCE:OCP:STATE?") ||
        scpi_handler_keyword_matches(keyword, "OCP:STAT?", ":SOURCE:OCP:STAT?")) {
        scpi_handler_handle_ocp_state_query(parsed, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OVP:PROTECT:STATE?", ":SOURCE:OVP:PROTECT:STATE?") ||
        scpi_handler_keyword_matches(keyword, "OVP:PROT:STAT?", ":SOURCE:OVP:PROT:STAT?")) {
        scpi_handler_handle_trip_query(parsed, true, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "OCP:PROTECT:STATE?", ":SOURCE:OCP:PROTECT:STATE?") ||
        scpi_handler_keyword_matches(keyword, "OCP:PROT:STAT?", ":SOURCE:OCP:PROT:STAT?")) {
        scpi_handler_handle_trip_query(parsed, false, write_response);
        return true;
    }

    if (scpi_handler_keyword_matches(keyword, "RESET:PROTECT", ":SOURCE:RESET:PROTECT") ||
        scpi_handler_keyword_matches(keyword, "RESET:PROT", ":SOURCE:RESET:PROT")) {
        scpi_handler_handle_reset_protection(parsed, write_response);
        return true;
    }

    return false;
}
