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
 * @file scpi_handler_common.c
 * @brief Shared SCPI parsing, formatting, and response helper functions.
 */

#include "scpi_handler_internal.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "scpi_common";

static void scpi_handler_trim_trailing(char *value)
{
    size_t length = strlen(value);
    while ((length > 0) && isspace((unsigned char)value[length - 1])) {
        value[--length] = '\0';
    }
}

void scpi_handler_trim(char *command)
{
    size_t start = 0;
    size_t end = strlen(command);

    while ((command[start] != '\0') && isspace((unsigned char)command[start])) {
        start++;
    }

    while ((end > start) && isspace((unsigned char)command[end - 1])) {
        end--;
    }

    if (start > 0) {
        memmove(command, command + start, end - start);
    }

    command[end - start] = '\0';
}

void scpi_handler_uppercase(char *command)
{
    for (size_t i = 0; command[i] != '\0'; ++i) {
        command[i] = (char)toupper((unsigned char)command[i]);
    }
}

void scpi_handler_parse_command(char *command, scpi_handler_parsed_command_t *parsed)
{
    parsed->keyword = command;
    parsed->argument = NULL;

    while ((*command != '\0') && !isspace((unsigned char)*command)) {
        command++;
    }

    if (*command == '\0') {
        return;
    }

    *command++ = '\0';

    while ((*command != '\0') && isspace((unsigned char)*command)) {
        command++;
    }

    if (*command == '\0') {
        return;
    }

    parsed->argument = command;
    scpi_handler_trim_trailing(parsed->argument);
}

void scpi_handler_write_esp_error(
    scpi_handler_write_response_fn_t write_response,
    const char *context,
    esp_err_t err)
{
    char response[96];
    snprintf(response, sizeof(response), "ERR,\"%s failed: %s\"", context, esp_err_to_name(err));
    write_response(response);
}

bool scpi_handler_parse_measure_channel(
    const char *argument,
    measure_channel_t *channel)
{
    char channel_upper[8];

    if ((argument == NULL) || (channel == NULL)) {
        return false;
    }

    strlcpy(channel_upper, argument, sizeof(channel_upper));
    scpi_handler_uppercase(channel_upper);

    if ((strcmp(channel_upper, "0") == 0) || (strcmp(channel_upper, "CH0") == 0)) {
        *channel = MEASURE_CHANNEL_0;
        return true;
    }

    if ((strcmp(channel_upper, "1") == 0) || (strcmp(channel_upper, "CH1") == 0)) {
        *channel = MEASURE_CHANNEL_1;
        return true;
    }

    return false;
}

bool scpi_handler_parse_size_arg(const char *argument, size_t *value)
{
    char *end;
    unsigned long parsed_value;

    if ((argument == NULL) || (value == NULL) || (argument[0] == '\0') || (argument[0] == '-')) {
        return false;
    }

    errno = 0;
    parsed_value = strtoul(argument, &end, 10);
    if ((end == argument) || (errno != 0)) {
        return false;
    }

    while ((*end != '\0') && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    *value = (size_t)parsed_value;
    return true;
}

bool scpi_handler_parse_value_u4(const char *argument, uint32_t *value_u4)
{
    char *end;
    double value;
    double scaled_value;

    if ((argument == NULL) || (value_u4 == NULL)) {
        return false;
    }

    errno = 0;
    value = strtod(argument, &end);
    if ((end == argument) || (errno != 0)) {
        return false;
    }

    while ((*end != '\0') && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    if (!(value >= 0.0)) {
        return false;
    }

    scaled_value = (value * (double)SCPI_HANDLER_VALUE_SCALE_U4) + 0.5;
    if (scaled_value > (double)UINT32_MAX) {
        ESP_LOGW(TAG, "scaled value exceeds uint32 range");
        return false;
    }

    *value_u4 = (uint32_t)scaled_value;
    return true;
}

bool scpi_handler_parse_seconds_to_ms(const char *argument, uint32_t *value_ms)
{
    char *end;
    double value_seconds;
    double scaled_ms;

    if ((argument == NULL) || (value_ms == NULL)) {
        return false;
    }

    errno = 0;
    value_seconds = strtod(argument, &end);
    if ((end == argument) || (errno != 0)) {
        return false;
    }

    while ((*end != '\0') && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    if (!(value_seconds >= 0.0)) {
        return false;
    }

    scaled_ms = (value_seconds * 1000.0) + 0.5;
    if (scaled_ms > (double)UINT32_MAX) {
        ESP_LOGW(TAG, "timer duration exceeds uint32 ms range");
        return false;
    }

    *value_ms = (uint32_t)scaled_ms;
    return true;
}

void scpi_handler_format_value_u4(
    uint32_t value_u4,
    char *response,
    size_t response_size)
{
    snprintf(
        response,
        response_size,
        "%" PRIu32 ".%04" PRIu32,
        value_u4 / SCPI_HANDLER_VALUE_SCALE_U4,
        value_u4 % SCPI_HANDLER_VALUE_SCALE_U4);
}

size_t scpi_handler_decimal_digits(size_t value)
{
    size_t digits = 1U;
    while (value >= 10U) {
        value /= 10U;
        digits++;
    }

    return digits;
}

void scpi_handler_write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)((value >> 8) & 0xFFU);
    destination[2] = (uint8_t)((value >> 16) & 0xFFU);
    destination[3] = (uint8_t)((value >> 24) & 0xFFU);
}
