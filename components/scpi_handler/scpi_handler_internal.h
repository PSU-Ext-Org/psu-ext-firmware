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
 * @file scpi_handler_internal.h
 * @brief Component-private SCPI parser helpers and command group entry points.
 */

#pragma once

#include "scpi_handler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"
#include "timebase_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCPI_HANDLER_COMMAND_BUFFER_SIZE 128
#define SCPI_HANDLER_IDN_RESPONSE "PSU-EXT,ESP32-S3,0001,0.1.0"
#define SCPI_HANDLER_VALUE_SCALE_U4 10000U

typedef struct {
    char *keyword;
    char *argument;
} scpi_handler_parsed_command_t;

/**
 * @brief Remove leading and trailing whitespace from a mutable string.
 *
 * @param command Null-terminated string to normalize in place.
 */
void scpi_handler_trim(char *command);

/**
 * @brief Convert a mutable string to uppercase in place.
 *
 * @param command Null-terminated string to normalize.
 */
void scpi_handler_uppercase(char *command);

/**
 * @brief Split a command line into keyword and optional argument text.
 *
 * @param command Mutable command line; whitespace after the keyword is replaced
 * with a null terminator.
 * @param parsed Output receiving keyword and argument pointers.
 */
void scpi_handler_parse_command(char *command, scpi_handler_parsed_command_t *parsed);

/**
 * @brief Format and emit an ESP-IDF error as a SCPI text error response.
 *
 * @param write_response Text response callback.
 * @param context Command context for the error message.
 * @param err ESP-IDF error code.
 */
void scpi_handler_write_esp_error(
    scpi_handler_write_response_fn_t write_response,
    const char *context,
    esp_err_t err);

/**
 * @brief Parse a measurement channel argument.
 *
 * @param argument Channel text such as `1` or `CH1`.
 * @param channel Output receiving the parsed channel.
 * @return `true` on success, otherwise `false`.
 */
bool scpi_handler_parse_measure_channel(
    const char *argument,
    measure_channel_t *channel);

/**
 * @brief Parse a non-negative decimal size argument.
 *
 * @param argument Decimal text to parse.
 * @param value Output receiving parsed value.
 * @return `true` on success, otherwise `false`.
 */
bool scpi_handler_parse_size_arg(const char *argument, size_t *value);

/**
 * @brief Parse a non-negative fixed-point decimal into units scaled by 10,000.
 *
 * @param argument Decimal value text.
 * @param value_u4 Output receiving scaled integer value.
 * @return `true` on success, otherwise `false`.
 */
bool scpi_handler_parse_value_u4(const char *argument, uint32_t *value_u4);

/**
 * @brief Parse a non-negative seconds value into rounded milliseconds.
 *
 * @param argument Decimal seconds text.
 * @param value_ms Output receiving the rounded millisecond value.
 * @return `true` on success, otherwise `false`.
 */
bool scpi_handler_parse_seconds_to_ms(const char *argument, uint32_t *value_ms);

/**
 * @brief Format a fixed-point value scaled by 10,000 as `whole.frac`.
 *
 * @param value_u4 Integer value scaled by 10,000.
 * @param response Destination buffer.
 * @param response_size Destination buffer size in bytes.
 */
void scpi_handler_format_value_u4(
    uint32_t value_u4,
    char *response,
    size_t response_size);

/**
 * @brief Count decimal digits needed to print an unsigned size value.
 *
 * @param value Value to measure.
 * @return Number of decimal digits.
 */
size_t scpi_handler_decimal_digits(size_t value);

/**
 * @brief Write a 32-bit unsigned value in little-endian byte order.
 *
 * @param destination Four-byte destination.
 * @param value Value to encode.
 */
void scpi_handler_write_u32_le(uint8_t *destination, uint32_t value);

/**
 * @brief Handle measurement-related SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param writer Response writer callbacks.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_measure_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer);

/**
 * @brief Handle SCPI system commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_system_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle output relay SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_output_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle protection SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_protection_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle calibration SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_calibration_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle WiFi configuration/status SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_wifi_command(
    const char *keyword,
    const scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle trigger configuration SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_trigger_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

/**
 * @brief Handle timer queue SCPI commands.
 *
 * @param keyword Uppercase command keyword.
 * @param parsed Parsed command payload.
 * @param write_response Text response callback.
 * @return `true` if the command was recognized.
 */
bool scpi_handler_handle_timer_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response);

#ifdef __cplusplus
}
#endif
