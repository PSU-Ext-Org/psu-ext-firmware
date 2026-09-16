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
 * @file scpi_handler_calibration.c
 * @brief Transactional multi-point calibration SCPI handlers.
 */

#include "scpi_handler_internal.h"
#include "measure_svc_calibration.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *short_form;
    const char *long_form;
} cal_keyword_segment_t;

static bool cal_segment_matches(
    const char *text,
    size_t length,
    const cal_keyword_segment_t *segment)
{
    return ((strlen(segment->short_form) == length) &&
            (strncmp(text, segment->short_form, length) == 0)) ||
        ((strlen(segment->long_form) == length) &&
         (strncmp(text, segment->long_form, length) == 0));
}

static bool cal_keyword_matches(
    const char *keyword,
    const cal_keyword_segment_t *segments,
    size_t segment_count,
    bool query)
{
    const char *cursor = keyword;
    if ((cursor == NULL) || (segments == NULL) || (segment_count == 0U)) {
        return false;
    }
    if (*cursor == ':') {
        ++cursor;
    }

    for (size_t index = 0U; index < segment_count; ++index) {
        const char *end = cursor;
        while ((*end != '\0') && (*end != ':') && (*end != '?')) {
            ++end;
        }
        if (!cal_segment_matches(cursor, (size_t)(end - cursor), &segments[index])) {
            return false;
        }

        if (index + 1U < segment_count) {
            if (*end != ':') {
                return false;
            }
            cursor = end + 1;
        } else {
            return query ? ((*end == '?') && (end[1] == '\0')) : (*end == '\0');
        }
    }
    return false;
}

static bool cal_command_matches(
    const char *keyword,
    const char *short_form,
    const char *long_form,
    bool query)
{
    const cal_keyword_segment_t segments[] = {
        {"CAL", "CALIBRATION"},
        {short_form, long_form},
    };
    return cal_keyword_matches(keyword, segments, 2U, query);
}

static bool cal_table_command_matches(
    const char *keyword,
    const char *quantity_short,
    const char *quantity_long,
    const char *action_short,
    const char *action_long,
    bool query)
{
    const cal_keyword_segment_t segments[] = {
        {"CAL", "CALIBRATION"},
        {quantity_short, quantity_long},
        {action_short, action_long},
    };
    return cal_keyword_matches(keyword, segments, 3U, query);
}

static bool cal_parse_quantity(const char *text, measure_kind_t *kind)
{
    char upper[12];
    if ((text == NULL) || (kind == NULL)) {
        return false;
    }

    strlcpy(upper, text, sizeof(upper));
    scpi_handler_uppercase(upper);
    if ((strcmp(upper, "VOLT") == 0) || (strcmp(upper, "VOLTAGE") == 0)) {
        *kind = MEASURE_KIND_VOLTAGE;
        return true;
    }
    if ((strcmp(upper, "CURR") == 0) || (strcmp(upper, "CURRENT") == 0)) {
        *kind = MEASURE_KIND_CURRENT;
        return true;
    }
    return false;
}

static bool cal_parse_start_args(
    char *argument,
    measure_kind_t *kind,
    measure_channel_t *channel)
{
    if ((argument == NULL) || (kind == NULL) || (channel == NULL)) {
        return false;
    }

    char *channel_arg = strchr(argument, ',');
    if (channel_arg == NULL) {
        return false;
    }
    *channel_arg++ = '\0';
    scpi_handler_trim(argument);
    scpi_handler_trim(channel_arg);

    return (strchr(channel_arg, ',') == NULL) &&
        cal_parse_quantity(argument, kind) &&
        scpi_handler_parse_measure_channel(channel_arg, channel);
}

static bool cal_parse_point_token(const char *text, uint8_t *point_index)
{
    char upper[12];
    if ((text == NULL) || (point_index == NULL)) {
        return false;
    }

    strlcpy(upper, text, sizeof(upper));
    scpi_handler_uppercase(upper);
    const char *digits = NULL;
    if (strncmp(upper, "POINT", 5U) == 0) {
        digits = &upper[5];
    } else if (strncmp(upper, "POIN", 4U) == 0) {
        digits = &upper[4];
    }
    if ((digits == NULL) || (*digits < '0') || (*digits > '9')) {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(digits, &end, 10);
    if ((*end != '\0') || (parsed == 0U) || (parsed > MEASURE_SVC_CAL_MAX_POINTS)) {
        return false;
    }
    *point_index = (uint8_t)parsed;
    return true;
}

static bool cal_parse_point_args(
    char *argument,
    bool expect_value,
    measure_channel_t *channel,
    uint8_t *point_index,
    uint32_t *actual_u4)
{
    if ((argument == NULL) || (channel == NULL) || (point_index == NULL)) {
        return false;
    }

    char *point_arg = strchr(argument, ',');
    if (point_arg == NULL) {
        return false;
    }
    *point_arg++ = '\0';
    scpi_handler_trim(argument);
    scpi_handler_trim(point_arg);

    char *value_arg = NULL;
    if (expect_value) {
        value_arg = strchr(point_arg, ',');
        if ((value_arg == NULL) || (actual_u4 == NULL)) {
            return false;
        }
        *value_arg++ = '\0';
        scpi_handler_trim(point_arg);
        scpi_handler_trim(value_arg);
    } else if (strchr(point_arg, ',') != NULL) {
        return false;
    }

    return scpi_handler_parse_measure_channel(argument, channel) &&
        cal_parse_point_token(point_arg, point_index) &&
        (!expect_value ||
         ((strchr(value_arg, ',') == NULL) && scpi_handler_parse_value_u4(value_arg, actual_u4)));
}

static bool cal_channel_supported(measure_kind_t kind, measure_channel_t channel)
{
    return ((kind == MEASURE_KIND_VOLTAGE) &&
            ((channel == MEASURE_CHANNEL_0) || (channel == MEASURE_CHANNEL_1))) ||
        ((kind == MEASURE_KIND_CURRENT) && (channel == MEASURE_CHANNEL_1));
}

static void cal_format_point(
    const measure_svc_cal_point_t *point,
    char *response,
    size_t response_size)
{
    int64_t raw_numerator =
        (int64_t)point->raw_code * point->pga_full_scale_mv * 1000;
    raw_numerator += raw_numerator >= 0 ? 16384 : -16384;
    const int32_t raw_voltage_u6 = (int32_t)(raw_numerator / 32768);
    const uint32_t raw_magnitude_u6 = raw_voltage_u6 < 0
        ? (uint32_t)(-(int64_t)raw_voltage_u6)
        : (uint32_t)raw_voltage_u6;
    snprintf(
        response,
        response_size,
        "%s%" PRIu32 ".%06" PRIu32 ",%" PRIu32 ".%04" PRIu32 ",%u.%03u",
        raw_voltage_u6 < 0 ? "-" : "",
        raw_magnitude_u6 / 1000000U,
        raw_magnitude_u6 % 1000000U,
        point->actual_voltage_u4 / SCPI_HANDLER_VALUE_SCALE_U4,
        point->actual_voltage_u4 % SCPI_HANDLER_VALUE_SCALE_U4,
        (unsigned)(point->pga_full_scale_mv / 1000U),
        (unsigned)(point->pga_full_scale_mv % 1000U));
}

static bool cal_require_no_argument(
    const scpi_handler_parsed_command_t *parsed,
    const char *expected,
    scpi_handler_write_response_fn_t write_response)
{
    if (parsed->argument == NULL) {
        return true;
    }
    char response[96];
    snprintf(response, sizeof(response), "ERR,\"Expected %s with no arguments\"", expected);
    write_response(response);
    return false;
}

static void cal_handle_start(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    measure_kind_t kind;
    measure_channel_t channel;
    if (!cal_parse_start_args(parsed->argument, &kind, &channel)) {
        write_response("ERR,\"Expected CALibration:STARt VOLTage,CH0|CH1 or CURRent,CH1\"");
        return;
    }
    if (!cal_channel_supported(kind, channel)) {
        write_response("ERR,\"Current calibration supports only channel 1|CH1\"");
        return;
    }

    const esp_err_t err = measure_svc_calibration_start(kind, channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "CALibration:STARt", err);
    }
}

static void cal_handle_transaction_query(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (!cal_require_no_argument(parsed, "CALibration:TRANsaction?", write_response)) {
        return;
    }

    measure_svc_cal_transaction_t transaction;
    const esp_err_t err = measure_svc_calibration_get_transaction(&transaction);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "CALibration:TRANsaction?", err);
        return;
    }
    if (transaction.state == MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        write_response("IDLE");
        return;
    }

    char response[32];
    snprintf(
        response,
        sizeof(response),
        "OPEN,%s,CH%u",
        transaction.kind == MEASURE_KIND_VOLTAGE ? "VOLTAGE" : "CURRENT",
        (unsigned)transaction.channel);
    write_response(response);
}

static void cal_handle_lifecycle(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response,
    bool commit)
{
    const char *context = commit ? "CALibration:COMMit" : "CALibration:ABORt";
    if (!cal_require_no_argument(parsed, context, write_response)) {
        return;
    }

    const esp_err_t err = commit ?
        measure_svc_calibration_commit() :
        measure_svc_calibration_abort();
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
    }
}

static void cal_handle_point(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response,
    measure_kind_t kind,
    bool query)
{
    measure_channel_t channel;
    uint8_t point_index;
    uint32_t actual_u4;
    if (!cal_parse_point_args(
            parsed->argument,
            !query,
            &channel,
            &point_index,
            &actual_u4)) {
        write_response(kind == MEASURE_KIND_VOLTAGE ?
            (query ?
                "ERR,\"Expected CALibration:VOLTage? CH0|CH1,POINt1|...|POINt32\"" :
                "ERR,\"Expected CALibration:VOLTage CH0|CH1,POINt1|...|POINt32,<actual>\"") :
            (query ?
                "ERR,\"Expected CALibration:CURRent? CH1,POINt1|...|POINt32\"" :
                "ERR,\"Expected CALibration:CURRent CH1,POINt1|...|POINt32,<actual>\""));
        return;
    }
    if (!cal_channel_supported(kind, channel)) {
        write_response("ERR,\"Current calibration supports only channel 1|CH1\"");
        return;
    }

    measure_svc_cal_point_t point;
    const esp_err_t err = query ?
        measure_svc_calibration_get_point(kind, channel, point_index, &point) :
        measure_svc_calibration_capture_point(kind, channel, point_index, actual_u4, NULL);
    const char *context = kind == MEASURE_KIND_VOLTAGE ?
        (query ? "CALibration:VOLTage?" : "CALibration:VOLTage") :
        (query ? "CALibration:CURRent?" : "CALibration:CURRent");
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
        return;
    }

    if (query) {
        char response[48];
        cal_format_point(&point, response, sizeof(response));
        write_response(response);
    }
}

static bool cal_parse_supported_channel(
    char *argument,
    measure_kind_t kind,
    measure_channel_t *channel,
    scpi_handler_write_response_fn_t write_response)
{
    if ((argument == NULL) || (strchr(argument, ',') != NULL) ||
        !scpi_handler_parse_measure_channel(argument, channel)) {
        write_response(kind == MEASURE_KIND_VOLTAGE ?
            "ERR,\"Expected channel 0|CH0 or 1|CH1\"" :
            "ERR,\"Expected channel 1 or CH1\"");
        return false;
    }
    if (!cal_channel_supported(kind, *channel)) {
        write_response("ERR,\"Current calibration supports only channel 1|CH1\"");
        return false;
    }
    return true;
}

static void cal_handle_clear(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response,
    measure_kind_t kind)
{
    measure_channel_t channel;
    if (!cal_parse_supported_channel(parsed->argument, kind, &channel, write_response)) {
        return;
    }
    const esp_err_t err = measure_svc_calibration_clear(kind, channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "CALibration:CLEar", err);
    }
}

static void cal_handle_count_query(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response,
    measure_kind_t kind)
{
    measure_channel_t channel;
    if (!cal_parse_supported_channel(parsed->argument, kind, &channel, write_response)) {
        return;
    }

    uint8_t count;
    const esp_err_t err = measure_svc_calibration_get_count(kind, channel, &count);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "CALibration:COUNt?", err);
        return;
    }
    char response[4];
    snprintf(response, sizeof(response), "%u", (unsigned)count);
    write_response(response);
}

bool scpi_handler_handle_calibration_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    if (cal_command_matches(keyword, "STAR", "START", false)) {
        cal_handle_start(parsed, write_response);
        return true;
    }
    if (cal_command_matches(keyword, "COMM", "COMMIT", false)) {
        cal_handle_lifecycle(parsed, write_response, true);
        return true;
    }
    if (cal_command_matches(keyword, "ABOR", "ABORT", false)) {
        cal_handle_lifecycle(parsed, write_response, false);
        return true;
    }
    if (cal_command_matches(keyword, "TRAN", "TRANSACTION", true)) {
        cal_handle_transaction_query(parsed, write_response);
        return true;
    }

    const bool voltage_set = cal_command_matches(keyword, "VOLT", "VOLTAGE", false);
    const bool voltage_query = cal_command_matches(keyword, "VOLT", "VOLTAGE", true);
    const bool current_set = cal_command_matches(keyword, "CURR", "CURRENT", false);
    const bool current_query = cal_command_matches(keyword, "CURR", "CURRENT", true);
    if (voltage_set || voltage_query) {
        cal_handle_point(parsed, write_response, MEASURE_KIND_VOLTAGE, voltage_query);
        return true;
    }
    if (current_set || current_query) {
        cal_handle_point(parsed, write_response, MEASURE_KIND_CURRENT, current_query);
        return true;
    }

    if (cal_table_command_matches(keyword, "VOLT", "VOLTAGE", "CLE", "CLEAR", false)) {
        cal_handle_clear(parsed, write_response, MEASURE_KIND_VOLTAGE);
        return true;
    }
    if (cal_table_command_matches(keyword, "CURR", "CURRENT", "CLE", "CLEAR", false)) {
        cal_handle_clear(parsed, write_response, MEASURE_KIND_CURRENT);
        return true;
    }
    if (cal_table_command_matches(keyword, "VOLT", "VOLTAGE", "COUN", "COUNT", true)) {
        cal_handle_count_query(parsed, write_response, MEASURE_KIND_VOLTAGE);
        return true;
    }
    if (cal_table_command_matches(keyword, "CURR", "CURRENT", "COUN", "COUNT", true)) {
        cal_handle_count_query(parsed, write_response, MEASURE_KIND_CURRENT);
        return true;
    }

    return false;
}
