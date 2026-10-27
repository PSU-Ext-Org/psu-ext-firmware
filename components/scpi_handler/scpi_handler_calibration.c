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
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    char *quantity;
    return scpi_handler_arg_string(&args, &quantity) &&
        cal_parse_quantity(quantity, kind) &&
        scpi_handler_arg_channel(&args, channel) && scpi_handler_args_end(&args);
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

static void command_cal_star(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    measure_kind_t kind;
    measure_channel_t channel;
    if (!cal_parse_start_args(parsed->argument, &kind, &channel)) {
        writer->write_text("ERR,\"Expected CALibration:STARt VOLTage,CH0|CH1 or CURRent,CH1\"");
        return;
    }
    if (!cal_channel_supported(kind, channel)) {
        writer->write_text("ERR,\"Current calibration supports only channel 1|CH1\"");
        return;
    }

    const esp_err_t err = measure_svc_calibration_start(kind, channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "CALibration:STARt", err);
    }
}

static void command_cal_tran_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    if (!cal_require_no_argument(parsed, "CALibration:TRANsaction?", writer->write_text)) {
        return;
    }

    measure_svc_cal_transaction_t transaction;
    const esp_err_t err = measure_svc_calibration_get_transaction(&transaction);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "CALibration:TRANsaction?", err);
        return;
    }
    if (transaction.state == MEASURE_SVC_CAL_TRANSACTION_IDLE) {
        writer->write_text("IDLE");
        return;
    }

    char response[32];
    snprintf(
        response,
        sizeof(response),
        "OPEN,%s,CH%u",
        transaction.kind == MEASURE_KIND_VOLTAGE ? "VOLTAGE" : "CURRENT",
        (unsigned)transaction.channel);
    writer->write_text(response);
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

static void command_cal_comm(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_lifecycle(parsed, writer->write_text, true);
}

static void command_cal_abor(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    cal_handle_lifecycle(parsed, writer->write_text, false);
}

static const scpi_handler_command_entry_t s_calibration_commands[] = {
    {"CAL:STAR", command_cal_star},
    {"CAL:START", command_cal_star},
    {"CALIBRATION:STAR", command_cal_star},
    {"CALIBRATION:START", command_cal_star},
    {":CAL:STAR", command_cal_star},
    {":CAL:START", command_cal_star},
    {":CALIBRATION:STAR", command_cal_star},
    {":CALIBRATION:START", command_cal_star},
    {"CAL:COMM", command_cal_comm},
    {"CAL:COMMIT", command_cal_comm},
    {"CALIBRATION:COMM", command_cal_comm},
    {"CALIBRATION:COMMIT", command_cal_comm},
    {":CAL:COMM", command_cal_comm},
    {":CAL:COMMIT", command_cal_comm},
    {":CALIBRATION:COMM", command_cal_comm},
    {":CALIBRATION:COMMIT", command_cal_comm},
    {"CAL:ABOR", command_cal_abor},
    {"CAL:ABORT", command_cal_abor},
    {"CALIBRATION:ABOR", command_cal_abor},
    {"CALIBRATION:ABORT", command_cal_abor},
    {":CAL:ABOR", command_cal_abor},
    {":CAL:ABORT", command_cal_abor},
    {":CALIBRATION:ABOR", command_cal_abor},
    {":CALIBRATION:ABORT", command_cal_abor},
    {"CAL:TRAN?", command_cal_tran_query},
    {"CAL:TRANSACTION?", command_cal_tran_query},
    {"CALIBRATION:TRAN?", command_cal_tran_query},
    {"CALIBRATION:TRANSACTION?", command_cal_tran_query},
    {":CAL:TRAN?", command_cal_tran_query},
    {":CAL:TRANSACTION?", command_cal_tran_query},
    {":CALIBRATION:TRAN?", command_cal_tran_query},
    {":CALIBRATION:TRANSACTION?", command_cal_tran_query},
};

const scpi_handler_command_module_t scpi_handler_calibration_module = {
    s_calibration_commands, sizeof(s_calibration_commands) / sizeof(s_calibration_commands[0]),
};
