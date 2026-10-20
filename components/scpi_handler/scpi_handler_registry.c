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

#include "scpi_handler_internal.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_registry_lock = portMUX_INITIALIZER_UNLOCKED;
static scpi_handler_command_t s_commands[SCPI_HANDLER_MAX_REGISTERED_COMMANDS];
static size_t s_command_count;

static bool valid_keyword(const char *keyword)
{
    if ((keyword == NULL) || (keyword[0] == '\0')) {
        return false;
    }
    for (size_t i = 0; keyword[i] != '\0'; ++i) {
        const char ch = keyword[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == ':' || ch == '*' || (ch == '?' && keyword[i + 1] == '\0'))) {
            return false;
        }
        /* Keep registration consistent with the dispatcher keyword buffer. */
        if (i >= SCPI_HANDLER_COMMAND_BUFFER_SIZE - 1U) {
            return false;
        }
    }
    return true;
}

esp_err_t scpi_handler_register_commands(
    const scpi_handler_command_t *commands,
    size_t count)
{
    if ((commands == NULL) || (count == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > SCPI_HANDLER_MAX_REGISTERED_COMMANDS) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!valid_keyword(commands[i].keyword) || commands[i].callback == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(commands[i].keyword, commands[j].keyword) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_registry_lock);
    for (size_t i = 0; i < count && result == ESP_OK; ++i) {
        for (size_t j = 0; j < s_command_count; ++j) {
            if (strcmp(commands[i].keyword, s_commands[j].keyword) == 0) {
                result = ESP_ERR_INVALID_STATE;
                break;
            }
        }
    }
    if ((result == ESP_OK) && (count > SCPI_HANDLER_MAX_REGISTERED_COMMANDS - s_command_count)) {
        result = ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK) {
        memcpy(&s_commands[s_command_count], commands, count * sizeof(*commands));
        s_command_count += count;
    }
    portEXIT_CRITICAL(&s_registry_lock);
    return result;
}

bool scpi_handler_dispatch_registered_command(
    const char *keyword,
    const char *argument,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_command_t matched = {0};
    portENTER_CRITICAL(&s_registry_lock);
    for (size_t i = 0; i < s_command_count; ++i) {
        if (strcmp(keyword, s_commands[i].keyword) == 0) {
            matched = s_commands[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_registry_lock);

    if (matched.callback == NULL) {
        return false;
    }
    matched.callback(argument, writer, matched.context);
    return true;
}
