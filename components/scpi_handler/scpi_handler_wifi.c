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

#include "scpi_handler_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_manager.h"

bool scpi_handler_handle_wifi_command(
    const char *keyword,
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (strcmp(keyword, "WIFI:SSID") == 0) {
        esp_err_t err;

        if (parsed->argument == NULL) {
            write_response("ERR,\"Missing SSID\"");
            return true;
        }

        err = wifi_manager_set_ssid(parsed->argument);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "WIFI:SSID", err);
        }
        return true;
    }

    if (strcmp(keyword, "WIFI:SSID?") == 0) {
        wifi_manager_state_t state;
        esp_err_t err = wifi_manager_get_state(&state);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "WIFI:SSID?", err);
            return true;
        }

        write_response(state.ssid[0] != '\0' ? state.ssid : "EMPTY");
        return true;
    }

    if (strcmp(keyword, "WIFI:PASS") == 0) {
        esp_err_t err;

        if (parsed->argument == NULL) {
            write_response("ERR,\"Missing password\"");
            return true;
        }

        err = wifi_manager_set_password(parsed->argument);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "WIFI:PASS", err);
        }
        return true;
    }

    if (strcmp(keyword, "WIFI:PASS?") == 0) {
        write_response(wifi_manager_password_is_set() ? "SET" : "EMPTY");
        return true;
    }

    if (strcmp(keyword, "WIFI:CLEAR") == 0) {
        esp_err_t err = wifi_manager_clear_credentials();
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "WIFI:CLEAR", err);
        }
        return true;
    }

    if (strcmp(keyword, "WIFI:STATUS?") == 0) {
        wifi_manager_state_t state;
        char response[160];
        esp_err_t err = wifi_manager_get_state(&state);
        if (err != ESP_OK) {
            scpi_handler_write_esp_error(write_response, "WIFI:STATUS?", err);
            return true;
        }

        snprintf(
            response,
            sizeof(response),
            "\"%s\",%d,%" PRIu32 ",\"%s\"",
            state.ssid,
            state.is_connected ? 1 : 0,
            state.conn_time_seconds,
            state.ip);
        write_response(response);
        return true;
    }

    return false;
}
