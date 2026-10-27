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
 * @file scpi_handler_wifi.c
 * @brief SCPI WiFi configuration and status command handlers.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_manager.h"

static void command_wifi_ssid(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    esp_err_t err;

    if (parsed->argument == NULL) {
        writer->write_text("ERR,\"Missing SSID\"");
        return;
    }

    err = wifi_manager_set_ssid(parsed->argument);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "WIFI:SSID", err);
    }
}

static void command_wifi_ssid_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    wifi_manager_state_t state;
    esp_err_t err = wifi_manager_get_state(&state);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "WIFI:SSID?", err);
        return;
    }

    writer->write_text(state.ssid[0] != '\0' ? state.ssid : "EMPTY");
}

static void command_wifi_pass(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    esp_err_t err;

    if (parsed->argument == NULL) {
        writer->write_text("ERR,\"Missing password\"");
        return;
    }

    err = wifi_manager_set_password(parsed->argument);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "WIFI:PASS", err);
    }
}

static void command_wifi_pass_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    writer->write_text(wifi_manager_password_is_set() ? "SET" : "EMPTY");
}

static void command_wifi_clear(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    esp_err_t err = wifi_manager_clear_credentials();
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "WIFI:CLEAR", err);
    }
}

static void command_wifi_status_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    wifi_manager_state_t state;
    char response[160];
    esp_err_t err = wifi_manager_get_state(&state);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "WIFI:STATUS?", err);
        return;
    }

    snprintf(
        response,
        sizeof(response),
        "\"%s\",%d,%" PRIu32 ",\"%s\"",
        state.ssid,
        state.is_connected ? 1 : 0,
        state.conn_time_seconds,
        state.ip);
    writer->write_text(response);
}

static const scpi_handler_command_entry_t s_wifi_commands[] = {
    {"WIFI:SSID", command_wifi_ssid},
    {"WIFI:SSID?", command_wifi_ssid_query},
    {"WIFI:PASS", command_wifi_pass},
    {"WIFI:PASS?", command_wifi_pass_query},
    {"WIFI:CLEAR", command_wifi_clear},
    {"WIFI:STATUS?", command_wifi_status_query},
};

const scpi_handler_command_module_t scpi_handler_wifi_module = {
    s_wifi_commands, sizeof(s_wifi_commands) / sizeof(s_wifi_commands[0]),
};
