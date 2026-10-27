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
 * @file scpi_handler_mqtt.c
 * @brief SCPI MQTT / Home Assistant configuration and status handlers.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mqtt_svc.h"

typedef enum {
    SCPI_HANDLER_MQTT_STRING_URI = 0,
    SCPI_HANDLER_MQTT_STRING_USER,
    SCPI_HANDLER_MQTT_STRING_PASSWORD,
    SCPI_HANDLER_MQTT_STRING_PREFIX,
} scpi_handler_mqtt_string_t;

/**
 * @brief Strip one pair of surrounding double quotes in place.
 *
 * Unquoted text is accepted as-is. A value with only one quote is rejected.
 */
static bool scpi_handler_mqtt_unquote(char *argument, char **value)
{
    size_t length;

    if (argument == NULL) {
        return false;
    }

    length = strlen(argument);
    if ((length >= 2U) && (argument[0] == '"') && (argument[length - 1U] == '"')) {
        argument[length - 1U] = '\0';
        *value = argument + 1;
        return strchr(*value, '"') == NULL;
    }

    if (strchr(argument, '"') != NULL) {
        return false;
    }

    *value = argument;
    return true;
}

static void scpi_handler_mqtt_write_quoted(
    scpi_handler_write_response_fn_t write_response,
    const char *value)
{
    char response[MQTT_SVC_URI_MAX_LENGTH + 3];
    snprintf(response, sizeof(response), "\"%s\"", value);
    write_response(response);
}

static void scpi_handler_mqtt_set_string(
    scpi_handler_mqtt_string_t field,
    const char *context,
    char *argument,
    scpi_handler_write_response_fn_t write_response)
{
    char *value = NULL;
    esp_err_t err;

    if (!scpi_handler_mqtt_unquote(argument, &value)) {
        char response[64];
        snprintf(response, sizeof(response), "ERR,\"Expected %s <quoted text>\"", context);
        write_response(response);
        return;
    }

    switch (field) {
    case SCPI_HANDLER_MQTT_STRING_URI:
        err = mqtt_svc_set_uri(value);
        break;
    case SCPI_HANDLER_MQTT_STRING_USER:
        err = mqtt_svc_set_user(value);
        break;
    case SCPI_HANDLER_MQTT_STRING_PASSWORD:
        err = mqtt_svc_set_password(value);
        break;
    case SCPI_HANDLER_MQTT_STRING_PREFIX:
    default:
        err = mqtt_svc_set_prefix(value);
        break;
    }

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
    }
}

static void scpi_handler_mqtt_query_string(
    esp_err_t (*getter)(char *, size_t),
    const char *context,
    scpi_handler_write_response_fn_t write_response)
{
    char value[MQTT_SVC_URI_MAX_LENGTH + 1];
    const esp_err_t err = getter(value, sizeof(value));

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
        return;
    }

    scpi_handler_mqtt_write_quoted(write_response, value);
}

static void scpi_handler_mqtt_set_enabled(
    const char *argument,
    scpi_handler_write_response_fn_t write_response)
{
    esp_err_t err;

    if ((argument == NULL) || ((strcmp(argument, "0") != 0) && (strcmp(argument, "1") != 0))) {
        write_response("ERR,\"Expected SYST:MQTT:ENAB <0|1>\"");
        return;
    }

    err = mqtt_svc_set_enabled(argument[0] == '1');
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "SYST:MQTT:ENAB", err);
    }
}

static void scpi_handler_mqtt_set_interval(
    const char *argument,
    scpi_handler_write_response_fn_t write_response)
{
    size_t interval_ms;
    esp_err_t err;

    if (!scpi_handler_parse_size_arg(argument, &interval_ms) ||
        (interval_ms < MQTT_SVC_MIN_INTERVAL_MS) ||
        (interval_ms > MQTT_SVC_MAX_INTERVAL_MS)) {
        write_response("ERR,\"Expected SYST:MQTT:INT <200..3600000>\"");
        return;
    }

    err = mqtt_svc_set_interval_ms((uint32_t)interval_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "SYST:MQTT:INT", err);
    }
}

static void scpi_handler_mqtt_query_status(scpi_handler_write_response_fn_t write_response)
{
    mqtt_svc_status_t status;
    char response[MQTT_SVC_CLIENT_ID_MAX_LENGTH + MQTT_SVC_ERROR_MAX_LENGTH + 24];
    const esp_err_t err = mqtt_svc_get_status(&status);

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "SYST:MQTT:STAT?", err);
        return;
    }

    snprintf(
        response,
        sizeof(response),
        "%s,\"%s\",\"%s\"",
        mqtt_svc_state_name(status.state),
        status.client_id,
        status.last_error);
    write_response(response);
}

static void command_syst_mqtt_uri(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_URI, "SYST:MQTT:URI", parsed->argument, writer->write_text);
}

static void command_syst_mqtt_uri_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_mqtt_query_string(mqtt_svc_get_uri, "SYST:MQTT:URI?", writer->write_text);
}

static void command_syst_mqtt_user(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_USER, "SYST:MQTT:USER", parsed->argument, writer->write_text);
}

static void command_syst_mqtt_user_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_mqtt_query_string(mqtt_svc_get_user, "SYST:MQTT:USER?", writer->write_text);
}

static void command_syst_mqtt_pass(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_string(
        SCPI_HANDLER_MQTT_STRING_PASSWORD,
        "SYST:MQTT:PASS",
        parsed->argument,
        writer->write_text);
}

static void command_syst_mqtt_pass_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    writer->write_text(mqtt_svc_password_is_set() ? "SET" : "EMPTY");
}

static void command_syst_mqtt_enab(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_enabled(parsed->argument, writer->write_text);
}

static void command_syst_mqtt_enab_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    bool enabled = false;
    const esp_err_t err = mqtt_svc_get_enabled(&enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "SYST:MQTT:ENAB?", err);
    } else {
        writer->write_text(enabled ? "1" : "0");
    }
}

static void command_syst_mqtt_int(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_interval(parsed->argument, writer->write_text);
}

static void command_syst_mqtt_int_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    uint32_t interval_ms = 0U;
    char response[16];
    const esp_err_t err = mqtt_svc_get_interval_ms(&interval_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "SYST:MQTT:INT?", err);
    } else {
        snprintf(response, sizeof(response), "%" PRIu32, interval_ms);
        writer->write_text(response);
    }
}

static void command_syst_mqtt_pref(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_PREFIX, "SYST:MQTT:PREF", parsed->argument, writer->write_text);
}

static void command_syst_mqtt_pref_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_mqtt_query_string(mqtt_svc_get_prefix, "SYST:MQTT:PREF?", writer->write_text);
}

static void command_syst_mqtt_stat_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_mqtt_query_status(writer->write_text);
}

static const scpi_handler_command_entry_t s_mqtt_commands[] = {
    {"SYST:MQTT:URI", command_syst_mqtt_uri},
    {"SYSTEM:MQTT:URI", command_syst_mqtt_uri},
    {":SYST:MQTT:URI", command_syst_mqtt_uri},
    {":SYSTEM:MQTT:URI", command_syst_mqtt_uri},
    {"SYST:MQTT:URI?", command_syst_mqtt_uri_query},
    {"SYSTEM:MQTT:URI?", command_syst_mqtt_uri_query},
    {":SYST:MQTT:URI?", command_syst_mqtt_uri_query},
    {":SYSTEM:MQTT:URI?", command_syst_mqtt_uri_query},
    {"SYST:MQTT:USER", command_syst_mqtt_user},
    {"SYSTEM:MQTT:USER", command_syst_mqtt_user},
    {":SYST:MQTT:USER", command_syst_mqtt_user},
    {":SYSTEM:MQTT:USER", command_syst_mqtt_user},
    {"SYST:MQTT:USER?", command_syst_mqtt_user_query},
    {"SYSTEM:MQTT:USER?", command_syst_mqtt_user_query},
    {":SYST:MQTT:USER?", command_syst_mqtt_user_query},
    {":SYSTEM:MQTT:USER?", command_syst_mqtt_user_query},
    {"SYST:MQTT:PASS", command_syst_mqtt_pass},
    {"SYSTEM:MQTT:PASS", command_syst_mqtt_pass},
    {":SYST:MQTT:PASS", command_syst_mqtt_pass},
    {":SYSTEM:MQTT:PASS", command_syst_mqtt_pass},
    {"SYST:MQTT:PASSWORD", command_syst_mqtt_pass},
    {"SYSTEM:MQTT:PASSWORD", command_syst_mqtt_pass},
    {":SYST:MQTT:PASSWORD", command_syst_mqtt_pass},
    {":SYSTEM:MQTT:PASSWORD", command_syst_mqtt_pass},
    {"SYST:MQTT:PASS?", command_syst_mqtt_pass_query},
    {"SYSTEM:MQTT:PASS?", command_syst_mqtt_pass_query},
    {":SYST:MQTT:PASS?", command_syst_mqtt_pass_query},
    {":SYSTEM:MQTT:PASS?", command_syst_mqtt_pass_query},
    {"SYST:MQTT:PASSWORD?", command_syst_mqtt_pass_query},
    {"SYSTEM:MQTT:PASSWORD?", command_syst_mqtt_pass_query},
    {":SYST:MQTT:PASSWORD?", command_syst_mqtt_pass_query},
    {":SYSTEM:MQTT:PASSWORD?", command_syst_mqtt_pass_query},
    {"SYST:MQTT:ENAB", command_syst_mqtt_enab},
    {"SYSTEM:MQTT:ENAB", command_syst_mqtt_enab},
    {":SYST:MQTT:ENAB", command_syst_mqtt_enab},
    {":SYSTEM:MQTT:ENAB", command_syst_mqtt_enab},
    {"SYST:MQTT:ENABLE", command_syst_mqtt_enab},
    {"SYSTEM:MQTT:ENABLE", command_syst_mqtt_enab},
    {":SYST:MQTT:ENABLE", command_syst_mqtt_enab},
    {":SYSTEM:MQTT:ENABLE", command_syst_mqtt_enab},
    {"SYST:MQTT:ENAB?", command_syst_mqtt_enab_query},
    {"SYSTEM:MQTT:ENAB?", command_syst_mqtt_enab_query},
    {":SYST:MQTT:ENAB?", command_syst_mqtt_enab_query},
    {":SYSTEM:MQTT:ENAB?", command_syst_mqtt_enab_query},
    {"SYST:MQTT:ENABLE?", command_syst_mqtt_enab_query},
    {"SYSTEM:MQTT:ENABLE?", command_syst_mqtt_enab_query},
    {":SYST:MQTT:ENABLE?", command_syst_mqtt_enab_query},
    {":SYSTEM:MQTT:ENABLE?", command_syst_mqtt_enab_query},
    {"SYST:MQTT:INT", command_syst_mqtt_int},
    {"SYSTEM:MQTT:INT", command_syst_mqtt_int},
    {":SYST:MQTT:INT", command_syst_mqtt_int},
    {":SYSTEM:MQTT:INT", command_syst_mqtt_int},
    {"SYST:MQTT:INTERVAL", command_syst_mqtt_int},
    {"SYSTEM:MQTT:INTERVAL", command_syst_mqtt_int},
    {":SYST:MQTT:INTERVAL", command_syst_mqtt_int},
    {":SYSTEM:MQTT:INTERVAL", command_syst_mqtt_int},
    {"SYST:MQTT:INT?", command_syst_mqtt_int_query},
    {"SYSTEM:MQTT:INT?", command_syst_mqtt_int_query},
    {":SYST:MQTT:INT?", command_syst_mqtt_int_query},
    {":SYSTEM:MQTT:INT?", command_syst_mqtt_int_query},
    {"SYST:MQTT:INTERVAL?", command_syst_mqtt_int_query},
    {"SYSTEM:MQTT:INTERVAL?", command_syst_mqtt_int_query},
    {":SYST:MQTT:INTERVAL?", command_syst_mqtt_int_query},
    {":SYSTEM:MQTT:INTERVAL?", command_syst_mqtt_int_query},
    {"SYST:MQTT:PREF", command_syst_mqtt_pref},
    {"SYSTEM:MQTT:PREF", command_syst_mqtt_pref},
    {":SYST:MQTT:PREF", command_syst_mqtt_pref},
    {":SYSTEM:MQTT:PREF", command_syst_mqtt_pref},
    {"SYST:MQTT:PREFIX", command_syst_mqtt_pref},
    {"SYSTEM:MQTT:PREFIX", command_syst_mqtt_pref},
    {":SYST:MQTT:PREFIX", command_syst_mqtt_pref},
    {":SYSTEM:MQTT:PREFIX", command_syst_mqtt_pref},
    {"SYST:MQTT:PREF?", command_syst_mqtt_pref_query},
    {"SYSTEM:MQTT:PREF?", command_syst_mqtt_pref_query},
    {":SYST:MQTT:PREF?", command_syst_mqtt_pref_query},
    {":SYSTEM:MQTT:PREF?", command_syst_mqtt_pref_query},
    {"SYST:MQTT:PREFIX?", command_syst_mqtt_pref_query},
    {"SYSTEM:MQTT:PREFIX?", command_syst_mqtt_pref_query},
    {":SYST:MQTT:PREFIX?", command_syst_mqtt_pref_query},
    {":SYSTEM:MQTT:PREFIX?", command_syst_mqtt_pref_query},
    {"SYST:MQTT:STAT?", command_syst_mqtt_stat_query},
    {"SYSTEM:MQTT:STAT?", command_syst_mqtt_stat_query},
    {":SYST:MQTT:STAT?", command_syst_mqtt_stat_query},
    {":SYSTEM:MQTT:STAT?", command_syst_mqtt_stat_query},
    {"SYST:MQTT:STATUS?", command_syst_mqtt_stat_query},
    {"SYSTEM:MQTT:STATUS?", command_syst_mqtt_stat_query},
    {":SYST:MQTT:STATUS?", command_syst_mqtt_stat_query},
    {":SYSTEM:MQTT:STATUS?", command_syst_mqtt_stat_query},
};

const scpi_handler_command_module_t scpi_handler_mqtt_module = {
    s_mqtt_commands, sizeof(s_mqtt_commands) / sizeof(s_mqtt_commands[0]),
};
