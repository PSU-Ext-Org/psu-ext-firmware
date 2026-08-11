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
 * @file scpi_handler_trigger.c
 * @brief SCPI trigger configuration command handlers.
 */

#include "scpi_handler_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "trigger_config.h"

static bool scpi_handler_parse_trigger_id(const char *argument, uint8_t *trigger_id)
{
    char *end;
    unsigned long parsed_value;

    if ((argument == NULL) || (trigger_id == NULL) || (argument[0] == '\0')) {
        return false;
    }

    parsed_value = strtoul(argument, &end, 10);
    if ((end == argument) || (*end != '\0') || (parsed_value > UINT8_MAX)) {
        return false;
    }

    *trigger_id = (uint8_t)parsed_value;
    return true;
}

static bool scpi_handler_parse_trigger_state(const char *argument, trigger_svc_state_t *state)
{
    char state_upper[8];

    if ((argument == NULL) || (state == NULL)) {
        return false;
    }

    strlcpy(state_upper, argument, sizeof(state_upper));
    scpi_handler_uppercase(state_upper);

    if (strcmp(state_upper, "LOW") == 0) {
        *state = TRIGGER_SVC_STATE_LOW;
        return true;
    }

    if (strcmp(state_upper, "HIGH") == 0) {
        *state = TRIGGER_SVC_STATE_HIGH;
        return true;
    }

    return false;
}

static bool scpi_handler_trigger_function_is_none(const char *function_name)
{
    char function_upper[8];

    if (function_name == NULL) {
        return false;
    }

    strlcpy(function_upper, function_name, sizeof(function_upper));
    scpi_handler_uppercase(function_upper);
    return strcmp(function_upper, "NONE") == 0;
}

static void scpi_handler_handle_trigger_config(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    char *state_arg;
    char *function_arg;
    char *arguments_arg;
    uint8_t trigger_id;
    trigger_svc_state_t state;
    esp_err_t err;

    if (parsed->argument == NULL) {
        write_response("ERR,\"Expected TRIG:CONF <trigger>,<HIGH|LOW>,<function>,<args...>\"");
        return;
    }

    state_arg = strchr(parsed->argument, ',');
    if (state_arg == NULL) {
        write_response("ERR,\"Expected TRIG:CONF <trigger>,<HIGH|LOW>,<function>,<args...>\"");
        return;
    }
    *state_arg++ = '\0';

    function_arg = strchr(state_arg, ',');
    if (function_arg == NULL) {
        write_response("ERR,\"Expected TRIG:CONF <trigger>,<HIGH|LOW>,<function>[,<args...>]\"");
        return;
    }
    *function_arg++ = '\0';

    arguments_arg = strchr(function_arg, ',');
    if (arguments_arg != NULL) {
        *arguments_arg++ = '\0';
        scpi_handler_trim(arguments_arg);
    } else {
        arguments_arg = function_arg + strlen(function_arg);
    }

    scpi_handler_trim(parsed->argument);
    scpi_handler_trim(state_arg);
    scpi_handler_trim(function_arg);

    if (!scpi_handler_parse_trigger_id(parsed->argument, &trigger_id)) {
        write_response("ERR,\"Expected trigger 1 or 2\"");
        return;
    }

    if (!scpi_handler_parse_trigger_state(state_arg, &state)) {
        write_response("ERR,\"Expected trigger state HIGH or LOW\"");
        return;
    }

    if (function_arg[0] == '\0') {
        write_response("ERR,\"Expected trigger function name\"");
        return;
    }

    if (scpi_handler_trigger_function_is_none(function_arg)) {
        if ((arguments_arg != NULL) && (arguments_arg[0] != '\0')) {
            write_response("ERR,\"NONE does not accept trigger arguments\"");
            return;
        }

        err = trigger_config_bind(trigger_id, state, "", "");
    } else {
        if ((arguments_arg == NULL) || (arguments_arg[0] == '\0')) {
            write_response("ERR,\"Expected trigger arguments\""); 
            return;
        }

        err = trigger_config_bind(trigger_id, state, function_arg, arguments_arg);
    }

    if (err == ESP_ERR_INVALID_ARG) {
        write_response("ERR,\"Invalid trigger function or arguments\"");
        return;
    }

    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TRIG:CONF", err);
    }
}

static void scpi_handler_handle_trigger_config_query(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    char *state_arg;
    uint8_t trigger_id;
    trigger_svc_state_t state;
    trigger_config_mapping_t mapping;
    char response[
        TRIGGER_CONFIG_FUNCTION_NAME_MAX_LEN +
        TRIGGER_CONFIG_ARGUMENTS_MAX_LEN +
        2U];
    esp_err_t err;

    if (parsed->argument == NULL) {
        write_response("ERR,\"Expected TRIG:CONF? <trigger>,<HIGH|LOW>\"");
        return;
    }

    state_arg = strchr(parsed->argument, ',');
    if (state_arg == NULL) {
        write_response("ERR,\"Expected TRIG:CONF? <trigger>,<HIGH|LOW>\"");
        return;
    }
    *state_arg++ = '\0';

    scpi_handler_trim(parsed->argument);
    scpi_handler_trim(state_arg);

    if ((parsed->argument[0] == '\0') || (state_arg[0] == '\0') || (strchr(state_arg, ',') != NULL)) {
        write_response("ERR,\"Expected TRIG:CONF? <trigger>,<HIGH|LOW>\"");
        return;
    }

    if (!scpi_handler_parse_trigger_id(parsed->argument, &trigger_id)) {
        write_response("ERR,\"Expected trigger 1 or 2\"");
        return;
    }

    if (!scpi_handler_parse_trigger_state(state_arg, &state)) {
        write_response("ERR,\"Expected trigger state HIGH or LOW\"");
        return;
    }

    err = trigger_config_get(trigger_id, state, &mapping);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "TRIG:CONF?", err);
        return;
    }

    if (mapping.function_name[0] == '\0') {
        write_response("NONE");
        return;
    }

    snprintf(response, sizeof(response), "%s,%s", mapping.function_name, mapping.arguments);
    write_response(response);
}

bool scpi_handler_handle_trigger_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (strcmp(keyword, "TRIG:CONF?") == 0) {
        scpi_handler_handle_trigger_config_query(parsed, write_response);
        return true;
    }

    if (strcmp(keyword, "TRIG:CONF") == 0) {
        scpi_handler_handle_trigger_config(parsed, write_response);
        return true;
    }

    return false;
}
