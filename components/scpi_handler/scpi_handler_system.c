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
 * @file scpi_handler_system.c
 * @brief SCPI system command handlers.
 */

#include "scpi_handler_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *SCPI_HANDLER_DATETIME_CONTEXT = "SYST:DATETIME";
static const char *SCPI_HANDLER_DATETIME_QUERY_CONTEXT = "SYST:DATETIME?";
static const char *SCPI_HANDLER_DATETIME_MAP_CONTEXT = "SYST:DATETIME:MAP?";
static const size_t SCPI_HANDLER_DATETIME_LENGTH = 23U;

typedef struct {
    time_t seconds;
    uint16_t millisecond;
} scpi_handler_time_parts_t;

static bool scpi_handler_system_keyword_matches(const char *keyword, const char *short_form, const char *long_form)
{
    if ((strcmp(keyword, short_form) == 0) || (strcmp(keyword, long_form) == 0)) {
        return true;
    }

    if ((keyword[0] == ':') && (strcmp(keyword + 1, short_form) == 0)) {
        return true;
    }

    if ((keyword[0] == ':') && (strcmp(keyword + 1, long_form) == 0)) {
        return true;
    }

    return false;
}

static bool scpi_handler_parse_millisecond_suffix(const char *text, uint16_t *millisecond)
{
    unsigned parsed_millisecond = 0U;
    int field_length = 0;
    char trailing = '\0';

    if ((text == NULL) || (millisecond == NULL)) {
        return false;
    }

    if (sscanf(text, "%3u%n%c", &parsed_millisecond, &field_length, &trailing) != 1) {
        return false;
    }

    if ((field_length != 3) || (text[3] != '\0') || (parsed_millisecond > 999U)) {
        return false;
    }

    *millisecond = (uint16_t)parsed_millisecond;
    return true;
}

static bool scpi_handler_parse_datetime_arg(const char *argument, struct tm *tm_value, uint16_t *millisecond)
{
    char roundtrip_prefix[20];
    char *parse_end = NULL;

    if ((argument == NULL) || (tm_value == NULL) || (millisecond == NULL) ||
        (strlen(argument) != SCPI_HANDLER_DATETIME_LENGTH)) {
        return false;
    }

    memset(tm_value, 0, sizeof(*tm_value));
    parse_end = strptime(argument, "%Y-%m-%d %H:%M:%S", tm_value);
    if ((parse_end == NULL) || (*parse_end != '.')) {
        return false;
    }

    if (!scpi_handler_parse_millisecond_suffix(parse_end + 1, millisecond)) {
        return false;
    }

    if ((tm_value->tm_year + 1900) < 1970) {
        return false;
    }

    if (strftime(roundtrip_prefix, sizeof(roundtrip_prefix), "%Y-%m-%d %H:%M:%S", tm_value) != 19U) {
        return false;
    }

    if (strncmp(argument, roundtrip_prefix, 19U) != 0) {
        return false;
    }

    return true;
}

static bool scpi_handler_datetime_to_time_parts(
    const struct tm *tm_value,
    uint16_t millisecond,
    scpi_handler_time_parts_t *time_parts)
{
    struct tm normalized_tm;
    time_t seconds;

    if ((tm_value == NULL) || (time_parts == NULL) || ((tm_value->tm_year + 1900) < 1970)) {
        return false;
    }

    normalized_tm = *tm_value;
    normalized_tm.tm_isdst = 0;

    seconds = timegm(&normalized_tm);
    if (seconds < 0) {
        return false;
    }

    if ((normalized_tm.tm_year != tm_value->tm_year) ||
        (normalized_tm.tm_mon != tm_value->tm_mon) ||
        (normalized_tm.tm_mday != tm_value->tm_mday) ||
        (normalized_tm.tm_hour != tm_value->tm_hour) ||
        (normalized_tm.tm_min != tm_value->tm_min) ||
        (normalized_tm.tm_sec != tm_value->tm_sec)) {
        return false;
    }

    time_parts->seconds = seconds;
    time_parts->millisecond = millisecond;
    return true;
}

static bool scpi_handler_datetime_to_unix_ms(const struct tm *tm_value, uint16_t millisecond, uint64_t *unix_ms)
{
    scpi_handler_time_parts_t time_parts;

    if ((tm_value == NULL) || (unix_ms == NULL)) {
        return false;
    }

    if (!scpi_handler_datetime_to_time_parts(tm_value, millisecond, &time_parts)) {
        return false;
    }

    *unix_ms = ((uint64_t)time_parts.seconds * 1000ULL) + (uint64_t)time_parts.millisecond;
    return true;
}

static bool scpi_handler_unix_ms_to_datetime(uint64_t unix_ms, struct tm *tm_value, uint16_t *millisecond)
{
    time_t seconds;

    if ((tm_value == NULL) || (millisecond == NULL)) {
        return false;
    }

    seconds = (time_t)(unix_ms / 1000ULL);
    if (gmtime_r(&seconds, tm_value) == NULL) {
        return false;
    }

    *millisecond = (uint16_t)(unix_ms % 1000ULL);
    return true;
}

static bool scpi_handler_format_datetime(
    const struct tm *tm_value,
    uint16_t millisecond,
    char *response,
    size_t response_size)
{
    size_t prefix_length;

    if ((tm_value == NULL) || (response == NULL) || (response_size < 24U)) {
        return false;
    }

    prefix_length = strftime(response, response_size, "%Y-%m-%d %H:%M:%S", tm_value);
    if (prefix_length != 19U) {
        return false;
    }

    return snprintf(response + prefix_length, response_size - prefix_length, ".%03u", (unsigned)millisecond) == 4;
}

static void scpi_handler_handle_system_datetime_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    struct tm tm_value;
    uint16_t millisecond;
    uint64_t unix_ms;
    esp_err_t err;

    if ((parsed->argument == NULL) || !scpi_handler_parse_datetime_arg(parsed->argument, &tm_value, &millisecond)) {
        write_response("ERR,\"Expected SYST:DATETIME YYYY-MM-DD HH:MM:SS.sss\"");
        return;
    }

    if (!scpi_handler_datetime_to_unix_ms(&tm_value, millisecond, &unix_ms)) {
        write_response("ERR,\"Datetime must be on or after 1970-01-01 00:00:00.000\"");
        return;
    }

    err = timebase_svc_set_base_unix_ms(unix_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, SCPI_HANDLER_DATETIME_CONTEXT, err);
    }
}

static void scpi_handler_handle_system_datetime_query(scpi_handler_write_response_fn_t write_response)
{
    struct tm tm_value;
    char response[32];
    uint64_t unix_ms;
    uint16_t millisecond;
    esp_err_t err;

    err = timebase_svc_get_current_unix_ms(&unix_ms);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, SCPI_HANDLER_DATETIME_QUERY_CONTEXT, err);
        return;
    }

    if (!scpi_handler_unix_ms_to_datetime(unix_ms, &tm_value, &millisecond)) {
        write_response("ERR,\"Failed to convert current datetime\"");
        return;
    }

    if (!scpi_handler_format_datetime(&tm_value, millisecond, response, sizeof(response))) {
        write_response("ERR,\"Failed to format current datetime\"");
        return;
    }

    write_response(response);
}

static void scpi_handler_handle_system_datetime_map_query(scpi_handler_write_response_fn_t write_response)
{
    struct tm tm_value;
    timebase_svc_mapping_t mapping;
    char timestamp[32];
    char response[48];
    uint16_t millisecond;
    esp_err_t err;

    err = timebase_svc_get_base_mapping(&mapping);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, SCPI_HANDLER_DATETIME_MAP_CONTEXT, err);
        return;
    }

    if (!scpi_handler_unix_ms_to_datetime(mapping.unix_ms, &tm_value, &millisecond)) {
        write_response("ERR,\"Failed to convert datetime mapping\"");
        return;
    }

    if (!scpi_handler_format_datetime(&tm_value, millisecond, timestamp, sizeof(timestamp))) {
        write_response("ERR,\"Failed to format datetime mapping\"");
        return;
    }

    snprintf(response, sizeof(response), "%s,%" PRIu32, timestamp, mapping.uptime_ms);
    write_response(response);
}

bool scpi_handler_handle_system_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (scpi_handler_system_keyword_matches(keyword, "SYST:ERR?", "SYSTEM:ERROR?")) {
        write_response("0,\"No error\"");
        return true;
    }

    if (scpi_handler_system_keyword_matches(keyword, "SYST:DATETIME", "SYSTEM:DATETIME")) {
        scpi_handler_handle_system_datetime_set(parsed, write_response);
        return true;
    }

    if (scpi_handler_system_keyword_matches(keyword, "SYST:DATETIME?", "SYSTEM:DATETIME?")) {
        scpi_handler_handle_system_datetime_query(write_response);
        return true;
    }

    if (scpi_handler_system_keyword_matches(keyword, "SYST:DATETIME:MAP?", "SYSTEM:DATETIME:MAP?")) {
        scpi_handler_handle_system_datetime_map_query(write_response);
        return true;
    }

    return false;
}
