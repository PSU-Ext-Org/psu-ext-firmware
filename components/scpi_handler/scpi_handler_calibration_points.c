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
 * @file scpi_handler_calibration_points.c
 * @brief Calibration point and table commands.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"
#include "measure_svc_calibration.h"
#include "scpi_handler_calibration.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    char *point;
    return scpi_handler_arg_channel(&args, channel) &&
        scpi_handler_arg_string(&args, &point) && cal_parse_point_token(point, point_index) &&
        (!expect_value || scpi_handler_arg_value(&args, actual_u4)) &&
        scpi_handler_args_end(&args);
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

static void command_cal_volt(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_point(parsed, writer->write_text, MEASURE_KIND_VOLTAGE, false);
}

static void command_cal_volt_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_point(parsed, writer->write_text, MEASURE_KIND_VOLTAGE, true);
}

static void command_cal_volt_cle(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_clear(parsed, writer->write_text, MEASURE_KIND_VOLTAGE);
}

static void command_cal_volt_coun_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_count_query(parsed, writer->write_text, MEASURE_KIND_VOLTAGE);
}

static void command_cal_curr(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_point(parsed, writer->write_text, MEASURE_KIND_CURRENT, false);
}

static void command_cal_curr_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_point(parsed, writer->write_text, MEASURE_KIND_CURRENT, true);
}

static void command_cal_curr_cle(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_clear(parsed, writer->write_text, MEASURE_KIND_CURRENT);
}

static void command_cal_curr_coun_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_count_query(parsed, writer->write_text, MEASURE_KIND_CURRENT);
}

static const scpi_handler_command_entry_t s_calibration_points_commands[] = {
    {"CAL:VOLT", command_cal_volt},
    {"CAL:VOLTAGE", command_cal_volt},
    {"CALIBRATION:VOLT", command_cal_volt},
    {"CALIBRATION:VOLTAGE", command_cal_volt},
    {":CAL:VOLT", command_cal_volt},
    {":CAL:VOLTAGE", command_cal_volt},
    {":CALIBRATION:VOLT", command_cal_volt},
    {":CALIBRATION:VOLTAGE", command_cal_volt},
    {"CAL:VOLT?", command_cal_volt_query},
    {"CAL:VOLTAGE?", command_cal_volt_query},
    {"CALIBRATION:VOLT?", command_cal_volt_query},
    {"CALIBRATION:VOLTAGE?", command_cal_volt_query},
    {":CAL:VOLT?", command_cal_volt_query},
    {":CAL:VOLTAGE?", command_cal_volt_query},
    {":CALIBRATION:VOLT?", command_cal_volt_query},
    {":CALIBRATION:VOLTAGE?", command_cal_volt_query},
    {"CAL:VOLT:CLE", command_cal_volt_cle},
    {"CAL:VOLT:CLEAR", command_cal_volt_cle},
    {"CAL:VOLTAGE:CLE", command_cal_volt_cle},
    {"CAL:VOLTAGE:CLEAR", command_cal_volt_cle},
    {"CALIBRATION:VOLT:CLE", command_cal_volt_cle},
    {"CALIBRATION:VOLT:CLEAR", command_cal_volt_cle},
    {"CALIBRATION:VOLTAGE:CLE", command_cal_volt_cle},
    {"CALIBRATION:VOLTAGE:CLEAR", command_cal_volt_cle},
    {":CAL:VOLT:CLE", command_cal_volt_cle},
    {":CAL:VOLT:CLEAR", command_cal_volt_cle},
    {":CAL:VOLTAGE:CLE", command_cal_volt_cle},
    {":CAL:VOLTAGE:CLEAR", command_cal_volt_cle},
    {":CALIBRATION:VOLT:CLE", command_cal_volt_cle},
    {":CALIBRATION:VOLT:CLEAR", command_cal_volt_cle},
    {":CALIBRATION:VOLTAGE:CLE", command_cal_volt_cle},
    {":CALIBRATION:VOLTAGE:CLEAR", command_cal_volt_cle},
    {"CAL:VOLT:COUN?", command_cal_volt_coun_query},
    {"CAL:VOLT:COUNT?", command_cal_volt_coun_query},
    {"CAL:VOLTAGE:COUN?", command_cal_volt_coun_query},
    {"CAL:VOLTAGE:COUNT?", command_cal_volt_coun_query},
    {"CALIBRATION:VOLT:COUN?", command_cal_volt_coun_query},
    {"CALIBRATION:VOLT:COUNT?", command_cal_volt_coun_query},
    {"CALIBRATION:VOLTAGE:COUN?", command_cal_volt_coun_query},
    {"CALIBRATION:VOLTAGE:COUNT?", command_cal_volt_coun_query},
    {":CAL:VOLT:COUN?", command_cal_volt_coun_query},
    {":CAL:VOLT:COUNT?", command_cal_volt_coun_query},
    {":CAL:VOLTAGE:COUN?", command_cal_volt_coun_query},
    {":CAL:VOLTAGE:COUNT?", command_cal_volt_coun_query},
    {":CALIBRATION:VOLT:COUN?", command_cal_volt_coun_query},
    {":CALIBRATION:VOLT:COUNT?", command_cal_volt_coun_query},
    {":CALIBRATION:VOLTAGE:COUN?", command_cal_volt_coun_query},
    {":CALIBRATION:VOLTAGE:COUNT?", command_cal_volt_coun_query},
    {"CAL:CURR", command_cal_curr},
    {"CAL:CURRENT", command_cal_curr},
    {"CALIBRATION:CURR", command_cal_curr},
    {"CALIBRATION:CURRENT", command_cal_curr},
    {":CAL:CURR", command_cal_curr},
    {":CAL:CURRENT", command_cal_curr},
    {":CALIBRATION:CURR", command_cal_curr},
    {":CALIBRATION:CURRENT", command_cal_curr},
    {"CAL:CURR?", command_cal_curr_query},
    {"CAL:CURRENT?", command_cal_curr_query},
    {"CALIBRATION:CURR?", command_cal_curr_query},
    {"CALIBRATION:CURRENT?", command_cal_curr_query},
    {":CAL:CURR?", command_cal_curr_query},
    {":CAL:CURRENT?", command_cal_curr_query},
    {":CALIBRATION:CURR?", command_cal_curr_query},
    {":CALIBRATION:CURRENT?", command_cal_curr_query},
    {"CAL:CURR:CLE", command_cal_curr_cle},
    {"CAL:CURR:CLEAR", command_cal_curr_cle},
    {"CAL:CURRENT:CLE", command_cal_curr_cle},
    {"CAL:CURRENT:CLEAR", command_cal_curr_cle},
    {"CALIBRATION:CURR:CLE", command_cal_curr_cle},
    {"CALIBRATION:CURR:CLEAR", command_cal_curr_cle},
    {"CALIBRATION:CURRENT:CLE", command_cal_curr_cle},
    {"CALIBRATION:CURRENT:CLEAR", command_cal_curr_cle},
    {":CAL:CURR:CLE", command_cal_curr_cle},
    {":CAL:CURR:CLEAR", command_cal_curr_cle},
    {":CAL:CURRENT:CLE", command_cal_curr_cle},
    {":CAL:CURRENT:CLEAR", command_cal_curr_cle},
    {":CALIBRATION:CURR:CLE", command_cal_curr_cle},
    {":CALIBRATION:CURR:CLEAR", command_cal_curr_cle},
    {":CALIBRATION:CURRENT:CLE", command_cal_curr_cle},
    {":CALIBRATION:CURRENT:CLEAR", command_cal_curr_cle},
    {"CAL:CURR:COUN?", command_cal_curr_coun_query},
    {"CAL:CURR:COUNT?", command_cal_curr_coun_query},
    {"CAL:CURRENT:COUN?", command_cal_curr_coun_query},
    {"CAL:CURRENT:COUNT?", command_cal_curr_coun_query},
    {"CALIBRATION:CURR:COUN?", command_cal_curr_coun_query},
    {"CALIBRATION:CURR:COUNT?", command_cal_curr_coun_query},
    {"CALIBRATION:CURRENT:COUN?", command_cal_curr_coun_query},
    {"CALIBRATION:CURRENT:COUNT?", command_cal_curr_coun_query},
    {":CAL:CURR:COUN?", command_cal_curr_coun_query},
    {":CAL:CURR:COUNT?", command_cal_curr_coun_query},
    {":CAL:CURRENT:COUN?", command_cal_curr_coun_query},
    {":CAL:CURRENT:COUNT?", command_cal_curr_coun_query},
    {":CALIBRATION:CURR:COUN?", command_cal_curr_coun_query},
    {":CALIBRATION:CURR:COUNT?", command_cal_curr_coun_query},
    {":CALIBRATION:CURRENT:COUN?", command_cal_curr_coun_query},
    {":CALIBRATION:CURRENT:COUNT?", command_cal_curr_coun_query},
};

const scpi_handler_command_module_t scpi_handler_calibration_points_module = {
    s_calibration_points_commands, sizeof(s_calibration_points_commands) / sizeof(s_calibration_points_commands[0]),
};
