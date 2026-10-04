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
 * @file scpi_handler.c
 * @brief Shared SCPI command dispatcher for PSU-EXT transports.
 */

#include "scpi_handler_internal.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "scpi_handler";

static bool scpi_handler_keyword_is_secret(const char *keyword_upper)
{
    const size_t length = strlen(keyword_upper);
    return (strstr(keyword_upper, ":PASS") != NULL) && (length > 0U) && (keyword_upper[length - 1U] != '?');
}

void scpi_handler_handle_command(
    char *command,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_parsed_command_t parsed;
    char keyword_upper[SCPI_HANDLER_COMMAND_BUFFER_SIZE];
    scpi_handler_write_response_fn_t write_response;

    if ((writer == NULL) || (writer->write_text == NULL)) {
        return;
    }
    write_response = writer->write_text;

    scpi_handler_trim(command);
    if (command[0] == '\0') {
        return;
    }

    scpi_handler_parse_command(command, &parsed);
    strlcpy(keyword_upper, parsed.keyword, sizeof(keyword_upper));
    scpi_handler_uppercase(keyword_upper);

    ESP_LOGI(
        TAG,
        "SCPI RX: %s%s%s",
        parsed.keyword,
        parsed.argument != NULL ? " " : "",
        parsed.argument == NULL ? "" :
            (scpi_handler_keyword_is_secret(keyword_upper) ? "***" : parsed.argument));

    if (strcmp(keyword_upper, "*IDN?") == 0) {
        write_response(SCPI_HANDLER_IDN_RESPONSE);
        return;
    }

    if (scpi_handler_handle_system_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_measure_command(keyword_upper, &parsed, writer) ||
        scpi_handler_handle_calibration_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_output_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_protection_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_timer_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_trigger_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_wifi_command(keyword_upper, &parsed, write_response) ||
        scpi_handler_handle_mqtt_command(keyword_upper, &parsed, write_response)) {
        return;
    }

    write_response("ERR,\"Unknown command\"");
}
