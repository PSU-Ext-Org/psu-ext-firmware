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

#include "scpi_handler_arguments.h"
#include <string.h>
#include <strings.h>

scpi_handler_args_t scpi_handler_args(char *text, bool allow_semicolon)
{
    if (text != NULL && allow_semicolon) {
        scpi_handler_trim(text);
        size_t length = strlen(text);
        if (length > 0 && text[length - 1] == ';') {
            text[length - 1] = '\0';
            scpi_handler_trim(text);
        }
    }
    return (scpi_handler_args_t){.next = text};
}

bool scpi_handler_arg_string(scpi_handler_args_t *args, char **value)
{
    if (args->failed || args->next == NULL) {
        args->failed = true;
        return false;
    }
    *value = args->next;
    args->next = strchr(*value, ',');
    if (args->next != NULL) {
        *args->next++ = '\0';
    }
    scpi_handler_trim(*value);
    args->failed = **value == '\0';
    return !args->failed;
}

bool scpi_handler_arg_channel(scpi_handler_args_t *args, measure_channel_t *value)
{
    char *token;
    bool ok = scpi_handler_arg_string(args, &token) &&
        scpi_handler_parse_measure_channel(token, value);
    args->failed = !ok;
    return ok;
}

bool scpi_handler_arg_value(scpi_handler_args_t *args, uint32_t *value)
{
    char *token;
    bool ok = scpi_handler_arg_string(args, &token) &&
        scpi_handler_parse_value_u4(token, value);
    args->failed = !ok;
    return ok;
}

bool scpi_handler_arg_bool(scpi_handler_args_t *args, bool allow_words, bool *value)
{
    char *token;
    if (!scpi_handler_arg_string(args, &token)) {
        return false;
    }
    if (strcmp(token, "1") == 0 || (allow_words && strcasecmp(token, "ON") == 0)) {
        *value = true;
        return true;
    }
    if (strcmp(token, "0") == 0 || (allow_words && strcasecmp(token, "OFF") == 0)) {
        *value = false;
        return true;
    }
    args->failed = true;
    return false;
}

bool scpi_handler_args_end(const scpi_handler_args_t *args)
{
    return !args->failed && args->next == NULL;
}
