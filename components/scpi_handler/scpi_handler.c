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

#include "scpi_handler_commands.h"

#include <string.h>

#include "esp_log.h"

#define SCPI_BUILTIN_MODULES(X) \
    X(system) \
    X(measure) \
    X(calibration_points) \
    X(measure_config) \
    X(data) \
    X(relative) \
    X(calibration) \
    X(output) \
    X(protection) \
    X(timer) \
    X(trigger) \
    X(wifi) \
    X(mqtt)

#define DECLARE_MODULE(name) extern const scpi_handler_command_module_t scpi_handler_##name##_module;
SCPI_BUILTIN_MODULES(DECLARE_MODULE)
#undef DECLARE_MODULE

#define MODULE_ADDRESS(name) &scpi_handler_##name##_module,
static const scpi_handler_command_module_t *const s_modules[] = {
    SCPI_BUILTIN_MODULES(MODULE_ADDRESS)
};
#undef MODULE_ADDRESS
#undef SCPI_BUILTIN_MODULES

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
    if (strlen(parsed.keyword) >= sizeof(keyword_upper)) {
        write_response("ERR,\"Command keyword too long\"");
        return;
    }
    strlcpy(keyword_upper, parsed.keyword, sizeof(keyword_upper));
    scpi_handler_uppercase(keyword_upper);

    ESP_LOGI(
        TAG,
        "SCPI RX: %s%s%s",
        parsed.keyword,
        parsed.argument != NULL ? " " : "",
        parsed.argument == NULL ? "" :
            (scpi_handler_keyword_is_secret(keyword_upper) ? "***" : parsed.argument));

    for (size_t module = 0; module < sizeof(s_modules) / sizeof(s_modules[0]); ++module) {
        for (size_t entry = 0; entry < s_modules[module]->count; ++entry) {
            const scpi_handler_command_entry_t *command = &s_modules[module]->commands[entry];
            if (strcmp(keyword_upper, command->keyword) == 0) {
                command->callback(&parsed, writer);
                return;
            }
        }
    }

    if (scpi_handler_dispatch_registered_command(keyword_upper, parsed.argument, writer)) {
        return;
    }

    write_response("ERR,\"Unknown command\"");
}
