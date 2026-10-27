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
 * @file scpi_handler_measure_config.c
 * @brief Averaging and ADC configuration commands.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *scpi_handler_measure_average_context(measure_kind_t kind, bool query)
{
    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        return query ? "MEAS:VOLT:AVER:COUN?" : "MEAS:VOLT:AVER:COUN";
    case MEASURE_KIND_CURRENT:
        return query ? "MEAS:CURR:AVER:COUN?" : "MEAS:CURR:AVER:COUN";
    case MEASURE_KIND_POWER:
        return query ? "MEAS:POWER:AVER:COUN?" : "MEAS:POWER:AVER:COUN";
    default:
        return query ? "MEAS:AVER:COUN?" : "MEAS:AVER:COUN";
    }
}

static void scpi_handler_handle_average_count_set(
    scpi_handler_parsed_command_t *parsed,
    measure_kind_t kind,
    scpi_handler_write_response_fn_t write_response)
{
    size_t count;
    esp_err_t err;
    const char *context = scpi_handler_measure_average_context(kind, false);

    if ((parsed->argument == NULL) || !scpi_handler_parse_size_arg(parsed->argument, &count)) {
        write_response("ERR,\"Expected averaging count\"");
        return;
    }

    err = measure_svc_set_average_count(kind, (uint32_t)count);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
    }
}

static void scpi_handler_handle_average_count_query(
    measure_kind_t kind,
    scpi_handler_write_response_fn_t write_response)
{
    uint32_t count;
    esp_err_t err;
    const char *context = scpi_handler_measure_average_context(kind, true);

    err = measure_svc_get_average_count(kind, &count);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
        return;
    }

    scpi_handler_respond_integer(write_response, count);
}

static void command_meas_adc_rate(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    size_t rate;
    if ((parsed->argument == NULL) ||
        !scpi_handler_parse_size_arg(parsed->argument, &rate) ||
        (rate > UINT16_MAX)) {
        writer->write_text("ERR,\"Expected ADS1115 rate: 8|16|32|64|128|250|475|860\"");
        return;
    }

    const esp_err_t err = measure_svc_set_adc_data_rate_sps((uint16_t)rate);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "MEAS:ADC:RATE", err);
    }
}

static void scpi_handler_handle_adc_rate_query(
    scpi_handler_write_response_fn_t write_response)
{
    uint16_t rate;
    const esp_err_t err = measure_svc_get_adc_data_rate_sps(&rate);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "MEAS:ADC:RATE?", err);
        return;
    }
    scpi_handler_respond_integer(write_response, rate);
}

static void command_meas_adc_rate_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_adc_rate_query(writer->write_text);
}

static void command_meas_volt_aver_coun(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_VOLTAGE, writer->write_text);
}

static void command_meas_volt_aver_coun_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_average_count_query(MEASURE_KIND_VOLTAGE, writer->write_text);
}

static void command_meas_curr_aver_coun(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_CURRENT, writer->write_text);
}

static void command_meas_curr_aver_coun_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_average_count_query(MEASURE_KIND_CURRENT, writer->write_text);
}

static void command_meas_power_aver_coun(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_POWER, writer->write_text);
}

static void command_meas_power_aver_coun_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    (void)parsed;
    scpi_handler_handle_average_count_query(MEASURE_KIND_POWER, writer->write_text);
}

static const scpi_handler_command_entry_t s_measure_config_commands[] = {
    {"MEAS:ADC:RATE", command_meas_adc_rate},
    {"MEASURE:ADC:RATE", command_meas_adc_rate},
    {"MEAS:ADC:RATE?", command_meas_adc_rate_query},
    {"MEASURE:ADC:RATE?", command_meas_adc_rate_query},
    {"MEAS:VOLT:AVER:COUN", command_meas_volt_aver_coun},
    {"MEAS:VOLT:AVER:COUN?", command_meas_volt_aver_coun_query},
    {"MEAS:CURR:AVER:COUN", command_meas_curr_aver_coun},
    {"MEAS:CURR:AVER:COUN?", command_meas_curr_aver_coun_query},
    {"MEAS:POWER:AVER:COUN", command_meas_power_aver_coun},
    {"MEAS:POWER:AVER:COUN?", command_meas_power_aver_coun_query},
};

const scpi_handler_command_module_t scpi_handler_measure_config_module = {
    s_measure_config_commands, sizeof(s_measure_config_commands) / sizeof(s_measure_config_commands[0]),
};
