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

#include "scpi_handler_internal.h"

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

static const char *scpi_handler_mqtt_subcommand(const char *keyword)
{
    if (keyword[0] == ':') {
        keyword++;
    }

    if (strncmp(keyword, "SYSTEM:MQTT:", 12) == 0) {
        return keyword + 12;
    }

    if (strncmp(keyword, "SYST:MQTT:", 10) == 0) {
        return keyword + 10;
    }

    return NULL;
}

static bool scpi_handler_mqtt_matches(const char *subcommand, const char *short_form, const char *long_form)
{
    return (strcmp(subcommand, short_form) == 0) || (strcmp(subcommand, long_form) == 0);
}

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

bool scpi_handler_handle_mqtt_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    const char *subcommand = scpi_handler_mqtt_subcommand(keyword);

    if (subcommand == NULL) {
        return false;
    }

    if (strcmp(subcommand, "URI") == 0) {
        scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_URI, "SYST:MQTT:URI", parsed->argument, write_response);
    } else if (strcmp(subcommand, "URI?") == 0) {
        scpi_handler_mqtt_query_string(mqtt_svc_get_uri, "SYST:MQTT:URI?", write_response);
    } else if (strcmp(subcommand, "USER") == 0) {
        scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_USER, "SYST:MQTT:USER", parsed->argument, write_response);
    } else if (strcmp(subcommand, "USER?") == 0) {
        scpi_handler_mqtt_query_string(mqtt_svc_get_user, "SYST:MQTT:USER?", write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "PASS", "PASSWORD")) {
        scpi_handler_mqtt_set_string(
            SCPI_HANDLER_MQTT_STRING_PASSWORD,
            "SYST:MQTT:PASS",
            parsed->argument,
            write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "PASS?", "PASSWORD?")) {
        write_response(mqtt_svc_password_is_set() ? "SET" : "EMPTY");
    } else if (scpi_handler_mqtt_matches(subcommand, "ENAB", "ENABLE")) {
        scpi_handler_mqtt_set_enabled(parsed->argument, write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "ENAB?", "ENABLE?")) {
        bool enabled = false;
        const esp_err_t err = mqtt_svc_get_enabled(&enabled);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "SYST:MQTT:ENAB?", err);
        } else {
            write_response(enabled ? "1" : "0");
        }
    } else if (scpi_handler_mqtt_matches(subcommand, "INT", "INTERVAL")) {
        scpi_handler_mqtt_set_interval(parsed->argument, write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "INT?", "INTERVAL?")) {
        uint32_t interval_ms = 0U;
        char response[16];
        const esp_err_t err = mqtt_svc_get_interval_ms(&interval_ms);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "SYST:MQTT:INT?", err);
        } else {
            snprintf(response, sizeof(response), "%" PRIu32, interval_ms);
            write_response(response);
        }
    } else if (scpi_handler_mqtt_matches(subcommand, "PREF", "PREFIX")) {
        scpi_handler_mqtt_set_string(SCPI_HANDLER_MQTT_STRING_PREFIX, "SYST:MQTT:PREF", parsed->argument, write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "PREF?", "PREFIX?")) {
        scpi_handler_mqtt_query_string(mqtt_svc_get_prefix, "SYST:MQTT:PREF?", write_response);
    } else if (scpi_handler_mqtt_matches(subcommand, "STAT?", "STATUS?")) {
        scpi_handler_mqtt_query_status(write_response);
    } else {
        return false;
    }

    return true;
}
