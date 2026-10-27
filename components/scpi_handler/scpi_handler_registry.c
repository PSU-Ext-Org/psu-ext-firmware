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

#include "scpi_handler_commands.h"
#include "scpi_handler_pattern.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

_Static_assert(SCPI_HANDLER_MAX_REGISTERED_COMMANDS <= 32, "Registry modes use a 32-bit mask");
static portMUX_TYPE s_registry_lock = portMUX_INITIALIZER_UNLOCKED;
static scpi_handler_command_t s_commands[SCPI_HANDLER_MAX_REGISTERED_COMMANDS];
static uint32_t s_pattern_mask;
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
        if (i >= SCPI_HANDLER_COMMAND_BUFFER_SIZE - 1U) {
            return false;
        }
    }
    return true;
}

typedef struct {
    const char *keyword;
    bool pattern;
} overlap_context_t;

static bool no_overlap(const char *keyword, void *context)
{
    const overlap_context_t *other = context;
    return !(other->pattern ? scpi_handler_pattern_match(other->keyword, keyword) :
        strcmp(other->keyword, keyword) == 0);
}

static bool overlaps(const char *first, bool first_pattern,
    const char *second, bool second_pattern)
{
    overlap_context_t other = {.keyword = second, .pattern = second_pattern};
    return first_pattern ? !scpi_handler_pattern_visit(first, no_overlap, &other) :
        !no_overlap(first, &other);
}

static esp_err_t register_table(const scpi_handler_command_t *commands,
    size_t count, bool patterns)
{
    if (commands == NULL || count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > SCPI_HANDLER_MAX_REGISTERED_COMMANDS) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < count; ++i) {
        if (commands[i].callback == NULL || !(patterns ?
                scpi_handler_pattern_valid(commands[i].keyword) :
                valid_keyword(commands[i].keyword))) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < i; ++j) {
            if (overlaps(commands[i].keyword, patterns, commands[j].keyword, patterns)) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    /* No unregister exists: a changed count is sufficient to detect a stale
     * snapshot. Keep validation and matching outside the spinlock. */
    for (;;) {
        scpi_handler_command_t snapshot[SCPI_HANDLER_MAX_REGISTERED_COMMANDS];
        size_t snapshot_count;
        uint32_t snapshot_mask;
        portENTER_CRITICAL(&s_registry_lock);
        snapshot_count = s_command_count;
        snapshot_mask = s_pattern_mask;
        memcpy(snapshot, s_commands, snapshot_count * sizeof(*snapshot));
        portEXIT_CRITICAL(&s_registry_lock);

        for (size_t i = 0; i < count; ++i) {
            for (size_t j = 0; j < snapshot_count; ++j) {
                if (overlaps(commands[i].keyword, patterns, snapshot[j].keyword,
                        (snapshot_mask & (UINT32_C(1) << j)) != 0)) {
                    return ESP_ERR_INVALID_STATE;
                }
            }
        }
        if (count > SCPI_HANDLER_MAX_REGISTERED_COMMANDS - snapshot_count) {
            return ESP_ERR_NO_MEM;
        }
        portENTER_CRITICAL(&s_registry_lock);
        if (s_command_count != snapshot_count) {
            portEXIT_CRITICAL(&s_registry_lock);
            continue;
        }
        memcpy(&s_commands[s_command_count], commands, count * sizeof(*commands));
        if (patterns) {
            for (size_t i = 0; i < count; ++i) {
                s_pattern_mask |= UINT32_C(1) << (s_command_count + i);
            }
        }
        s_command_count += count;
        portEXIT_CRITICAL(&s_registry_lock);
        return ESP_OK;
    }
}

esp_err_t scpi_handler_register_commands(const scpi_handler_command_t *commands, size_t count)
{
    return register_table(commands, count, false);
}

esp_err_t scpi_handler_register_patterns(const scpi_handler_command_t *commands, size_t count)
{
    return register_table(commands, count, true);
}

bool scpi_handler_dispatch_registered_command(
    const char *keyword, const char *argument,
    const scpi_handler_response_writer_t *writer)
{
    size_t count;
    portENTER_CRITICAL(&s_registry_lock);
    count = s_command_count;
    portEXIT_CRITICAL(&s_registry_lock);

    for (size_t i = 0; i < count; ++i) {
        scpi_handler_command_t command;
        bool pattern;
        portENTER_CRITICAL(&s_registry_lock);
        command = s_commands[i];
        pattern = (s_pattern_mask & (UINT32_C(1) << i)) != 0;
        portEXIT_CRITICAL(&s_registry_lock);
        if (pattern ? scpi_handler_pattern_match(command.keyword, keyword) :
                strcmp(command.keyword, keyword) == 0) {
            command.callback(argument, writer, command.context);
            return true;
        }
    }
    return false;
}
