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
 * @file scpi_handler_protection.c
 * @brief SCPI protection command handlers.
 */

#include "scpi_handler_arguments.h"
#include "scpi_handler_responses.h"
#include "scpi_handler_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protection_svc.h"

static bool scpi_handler_parse_channel_value_args(char *argument, uint8_t *channel, uint32_t *value_u4)
{
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    measure_channel_t parsed_channel;
    if (!scpi_handler_arg_channel(&args, &parsed_channel) ||
        !scpi_handler_arg_value(&args, value_u4) || !scpi_handler_args_end(&args)) {
        return false;
    }
    *channel = (uint8_t)parsed_channel;
    return true;
}

static bool scpi_handler_parse_channel_bool_args(char *argument, uint8_t *channel, bool *enabled)
{
    scpi_handler_args_t args = scpi_handler_args(argument, false);
    measure_channel_t parsed_channel;
    if (!scpi_handler_arg_channel(&args, &parsed_channel) ||
        !scpi_handler_arg_bool(&args, true, enabled) || !scpi_handler_args_end(&args)) {
        return false;
    }
    *channel = (uint8_t)parsed_channel;
    return true;
}

static bool scpi_handler_parse_channel_arg(const char *argument, uint8_t *channel)
{
    measure_channel_t parsed_channel;

    if ((argument == NULL) || (channel == NULL)) {
        return false;
    }

    if (!scpi_handler_parse_measure_channel(argument, &parsed_channel)) {
        return false;
    }

    *channel = (uint8_t)parsed_channel;
    return true;
}

static void command_ovp(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_value_args(parsed->argument, &channel, &value_u4)) {
        writer->write_text("ERR,\"Expected OVP CH0|CH1,<value>\"");
        return;
    }

    err = protection_svc_set_ovp_u4(channel, value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OVP", err);
    }
}

static void command_ovp_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 0|CH0 or 1|CH1\"");
        return;
    }

    err = protection_svc_get_ovp_u4(channel, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OVP?", err);
        return;
    }

    scpi_handler_respond_value(writer->write_text, value_u4);
}

static void command_ocp(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_value_args(parsed->argument, &channel, &value_u4)) {
        writer->write_text("ERR,\"Expected OCP CH1,<value>\"");
        return;
    }

    err = protection_svc_set_ocp_u4(channel, value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OCP", err);
    }
}

static void command_ocp_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    uint32_t value_u4;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = protection_svc_get_ocp_u4(channel, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OCP?", err);
        return;
    }

    scpi_handler_respond_value(writer->write_text, value_u4);
}

static void command_ocp_state(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_channel_bool_args(parsed->argument, &channel, &enabled)) {
        writer->write_text("ERR,\"Expected OCP:STATe CH1,<0|1|OFF|ON>\"");
        return;
    }

    err = protection_svc_set_ocp_enabled(channel, enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OCP:STAT", err);
    }
}

static void command_ocp_state_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    bool enabled;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = protection_svc_get_ocp_enabled(channel, &enabled);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "OCP:STAT?", err);
        return;
    }

    scpi_handler_respond_bool(writer->write_text, enabled);
}

static void scpi_handler_handle_trip_query(
    const scpi_handler_parsed_command_t *parsed,
    bool ovp,
    scpi_handler_write_response_fn_t write_response)
{
    uint8_t channel;
    bool tripped;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        write_response(ovp ? "ERR,\"Expected channel 0|CH0 or 1|CH1\"" : "ERR,\"Expected channel 1 or CH1\"");
        return;
    }

    err = ovp ?
        protection_svc_get_ovp_tripped(channel, &tripped) :
        protection_svc_get_ocp_tripped(channel, &tripped);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, ovp ? "OVP:PROT:STAT?" : "OCP:PROT:STAT?", err);
        return;
    }

    write_response(tripped ? "1" : "0");
}

static void command_reset_protect(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    uint8_t channel;
    esp_err_t err;

    if (!scpi_handler_parse_channel_arg(parsed->argument, &channel)) {
        writer->write_text("ERR,\"Expected channel 0|CH0 or 1|CH1\"");
        return;
    }

    err = protection_svc_clear_trips(channel);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, "RESET:PROT", err);
    }
}

static void command_ovp_protect_state_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_trip_query(parsed, true, writer->write_text);
}

static void command_ocp_protect_state_query(
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_handle_trip_query(parsed, false, writer->write_text);
}

static const scpi_handler_command_entry_t s_protection_commands[] = {
    {"OVP", command_ovp},
    {":SOURCE:OVP", command_ovp},
    {":SOUR:OVP", command_ovp},
    {"SOURCE:OVP", command_ovp},
    {"SOUR:OVP", command_ovp},
    {"OVP?", command_ovp_query},
    {":SOURCE:OVP?", command_ovp_query},
    {":SOUR:OVP?", command_ovp_query},
    {"SOURCE:OVP?", command_ovp_query},
    {"SOUR:OVP?", command_ovp_query},
    {"OCP", command_ocp},
    {":SOURCE:OCP", command_ocp},
    {":SOUR:OCP", command_ocp},
    {"SOURCE:OCP", command_ocp},
    {"SOUR:OCP", command_ocp},
    {"OCP?", command_ocp_query},
    {":SOURCE:OCP?", command_ocp_query},
    {":SOUR:OCP?", command_ocp_query},
    {"SOURCE:OCP?", command_ocp_query},
    {"SOUR:OCP?", command_ocp_query},
    {"OCP:STATE", command_ocp_state},
    {":SOURCE:OCP:STATE", command_ocp_state},
    {":SOUR:OCP:STATE", command_ocp_state},
    {"SOURCE:OCP:STATE", command_ocp_state},
    {"SOUR:OCP:STATE", command_ocp_state},
    {"OCP:STAT", command_ocp_state},
    {":SOURCE:OCP:STAT", command_ocp_state},
    {":SOUR:OCP:STAT", command_ocp_state},
    {"SOURCE:OCP:STAT", command_ocp_state},
    {"SOUR:OCP:STAT", command_ocp_state},
    {"OCP:STATE?", command_ocp_state_query},
    {":SOURCE:OCP:STATE?", command_ocp_state_query},
    {":SOUR:OCP:STATE?", command_ocp_state_query},
    {"SOURCE:OCP:STATE?", command_ocp_state_query},
    {"SOUR:OCP:STATE?", command_ocp_state_query},
    {"OCP:STAT?", command_ocp_state_query},
    {":SOURCE:OCP:STAT?", command_ocp_state_query},
    {":SOUR:OCP:STAT?", command_ocp_state_query},
    {"SOURCE:OCP:STAT?", command_ocp_state_query},
    {"SOUR:OCP:STAT?", command_ocp_state_query},
    {"OVP:PROTECT:STATE?", command_ovp_protect_state_query},
    {":SOURCE:OVP:PROTECT:STATE?", command_ovp_protect_state_query},
    {":SOUR:OVP:PROTECT:STATE?", command_ovp_protect_state_query},
    {"SOURCE:OVP:PROTECT:STATE?", command_ovp_protect_state_query},
    {"SOUR:OVP:PROTECT:STATE?", command_ovp_protect_state_query},
    {"OVP:PROT:STAT?", command_ovp_protect_state_query},
    {":SOURCE:OVP:PROT:STAT?", command_ovp_protect_state_query},
    {":SOUR:OVP:PROT:STAT?", command_ovp_protect_state_query},
    {"SOURCE:OVP:PROT:STAT?", command_ovp_protect_state_query},
    {"SOUR:OVP:PROT:STAT?", command_ovp_protect_state_query},
    {"OCP:PROTECT:STATE?", command_ocp_protect_state_query},
    {":SOURCE:OCP:PROTECT:STATE?", command_ocp_protect_state_query},
    {":SOUR:OCP:PROTECT:STATE?", command_ocp_protect_state_query},
    {"SOURCE:OCP:PROTECT:STATE?", command_ocp_protect_state_query},
    {"SOUR:OCP:PROTECT:STATE?", command_ocp_protect_state_query},
    {"OCP:PROT:STAT?", command_ocp_protect_state_query},
    {":SOURCE:OCP:PROT:STAT?", command_ocp_protect_state_query},
    {":SOUR:OCP:PROT:STAT?", command_ocp_protect_state_query},
    {"SOURCE:OCP:PROT:STAT?", command_ocp_protect_state_query},
    {"SOUR:OCP:PROT:STAT?", command_ocp_protect_state_query},
    {"RESET:PROTECT", command_reset_protect},
    {":SOURCE:RESET:PROTECT", command_reset_protect},
    {":SOUR:RESET:PROTECT", command_reset_protect},
    {"SOURCE:RESET:PROTECT", command_reset_protect},
    {"SOUR:RESET:PROTECT", command_reset_protect},
    {"RESET:PROT", command_reset_protect},
    {":SOURCE:RESET:PROT", command_reset_protect},
    {":SOUR:RESET:PROT", command_reset_protect},
    {"SOURCE:RESET:PROT", command_reset_protect},
    {"SOUR:RESET:PROT", command_reset_protect},
};

const scpi_handler_command_module_t scpi_handler_protection_module = {
    s_protection_commands, sizeof(s_protection_commands) / sizeof(s_protection_commands[0]),
};
