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
 * @file scpi_handler_measure.c
 * @brief SCPI measurement query and binary voltage history handlers.
 */

#include "scpi_handler_internal.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SCPI_HANDLER_DATA_RECORD_SIZE 8U
#define SCPI_HANDLER_DATA_RESPONSE_BUFFER_SIZE \
    (16U + (MEASURE_SVC_MAX_SAMPLE_CAPACITY * SCPI_HANDLER_DATA_RECORD_SIZE))

static const char *TAG = "scpi_measure";
static SemaphoreHandle_t s_data_response_lock;
static measure_svc_sample_t *s_data_samples;
static uint8_t *s_data_response_buffer;

typedef struct {
    measure_channel_t channel;
    size_t count;
    size_t start_offset;
} scpi_handler_measure_data_args_t;

static bool scpi_handler_parse_measure_data_args(
    char *argument,
    scpi_handler_measure_data_args_t *args)
{
    char *count_arg;
    char *offset_arg;

    if ((argument == NULL) || (args == NULL)) {
        return false;
    }

    args->count = MEASURE_SVC_MAX_SAMPLE_CAPACITY;
    args->start_offset = 0U;

    count_arg = strchr(argument, ',');
    if (count_arg != NULL) {
        *count_arg++ = '\0';
        scpi_handler_trim(argument);
        scpi_handler_trim(count_arg);

        offset_arg = strchr(count_arg, ',');
        if (offset_arg != NULL) {
            *offset_arg++ = '\0';
            scpi_handler_trim(count_arg);
            scpi_handler_trim(offset_arg);
        }

        if (!scpi_handler_parse_size_arg(count_arg, &args->count)) {
            return false;
        }

        if ((offset_arg != NULL) && !scpi_handler_parse_size_arg(offset_arg, &args->start_offset)) {
            return false;
        }
    } else {
        scpi_handler_trim(argument);
    }

    return scpi_handler_parse_measure_channel(argument, &args->channel);
}

static const char *scpi_handler_measure_query_context(measure_kind_t kind)
{
    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        return "MEAS:VOLT?";
    case MEASURE_KIND_CURRENT:
        return "MEAS:CURR?";
    case MEASURE_KIND_POWER:
        return "MEAS:POWER?";
    default:
        return "MEAS?";
    }
}

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

static const char *scpi_handler_measure_data_context(measure_kind_t kind)
{
    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        return "MEAS:VOLT:DATA?";
    case MEASURE_KIND_CURRENT:
        return "MEAS:CURR:DATA?";
    case MEASURE_KIND_POWER:
        return "MEAS:POWER:DATA?";
    default:
        return "MEAS:DATA?";
    }
}

static esp_err_t scpi_handler_take_data_response_lock(void)
{
    if (s_data_response_lock == NULL) {
        s_data_response_lock = xSemaphoreCreateMutex();
        if (s_data_response_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_data_samples == NULL) {
        s_data_samples = heap_caps_malloc(
            MEASURE_SVC_MAX_SAMPLE_CAPACITY * sizeof(*s_data_samples),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_data_samples == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_data_response_buffer == NULL) {
        s_data_response_buffer = heap_caps_malloc(
            SCPI_HANDLER_DATA_RESPONSE_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_data_response_buffer == NULL) {
            heap_caps_free(s_data_samples);
            s_data_samples = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    return xSemaphoreTake(s_data_response_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void scpi_handler_handle_measure_query(
    const scpi_handler_parsed_command_t *parsed,
    measure_kind_t kind,
    scpi_handler_write_response_fn_t write_response)
{
    measure_channel_t channel;
    uint32_t value_u4;
    char response[24];
    esp_err_t err;

    if (!scpi_handler_parse_measure_channel(parsed->argument, &channel)) {
        write_response("ERR,\"Expected measurement channel\"");
        return;
    }

    err = measure_svc_read(channel, kind, &value_u4);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(
            write_response,
            scpi_handler_measure_query_context(kind),
            err);
        return;
    }

    scpi_handler_format_value_u4(value_u4, response, sizeof(response));
    write_response(response);
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
    char response[16];
    esp_err_t err;
    const char *context = scpi_handler_measure_average_context(kind, true);

    err = measure_svc_get_average_count(kind, &count);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, context, err);
        return;
    }

    snprintf(response, sizeof(response), "%" PRIu32, count);
    write_response(response);
}

static void scpi_handler_handle_adc_rate_set(
    scpi_handler_parsed_command_t *parsed,
    scpi_handler_write_response_fn_t write_response)
{
    size_t rate;
    if ((parsed->argument == NULL) ||
        !scpi_handler_parse_size_arg(parsed->argument, &rate) ||
        (rate > UINT16_MAX)) {
        write_response("ERR,\"Expected ADS1115 rate: 8|16|32|64|128|250|475|860\"");
        return;
    }

    const esp_err_t err = measure_svc_set_adc_data_rate_sps((uint16_t)rate);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "MEAS:ADC:RATE", err);
    }
}

static void scpi_handler_handle_adc_rate_query(
    scpi_handler_write_response_fn_t write_response)
{
    uint16_t rate;
    char response[8];
    const esp_err_t err = measure_svc_get_adc_data_rate_sps(&rate);
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(write_response, "MEAS:ADC:RATE?", err);
        return;
    }
    snprintf(response, sizeof(response), "%u", (unsigned)rate);
    write_response(response);
}

static void scpi_handler_handle_measure_data_query(
    scpi_handler_parsed_command_t *parsed,
    measure_kind_t kind,
    const scpi_handler_response_writer_t *writer)
{
    scpi_handler_measure_data_args_t args;
    size_t copied_count = 0U;
    const char *context = scpi_handler_measure_data_context(kind);

    if ((writer == NULL) || (writer->write_text == NULL)) {
        return;
    }

    if (writer->write_binary == NULL) {
        writer->write_text("ERR,\"Binary response not supported\"");
        return;
    }

    if (!scpi_handler_parse_measure_data_args(parsed->argument, &args)) {
        writer->write_text("ERR,\"Expected DATA query channel 1|CH1[,<count>[,<start_offset>]]\"");
        return;
    }

    if (args.count > MEASURE_SVC_MAX_SAMPLE_CAPACITY) {
        args.count = MEASURE_SVC_MAX_SAMPLE_CAPACITY;
    }

    esp_err_t err = scpi_handler_take_data_response_lock();
    if (err != ESP_OK) {
        scpi_handler_write_esp_error(writer->write_text, context, err);
        return;
    }

    if (kind == MEASURE_KIND_CURRENT) {
        err = measure_svc_copy_current_samples(
            args.channel,
            args.start_offset,
            args.count,
            s_data_samples,
            &copied_count);
    } else if (kind == MEASURE_KIND_POWER) {
        err = measure_svc_copy_power_samples(
            args.channel,
            args.start_offset,
            args.count,
            s_data_samples,
            &copied_count);
    } else {
        err = measure_svc_copy_voltage_samples(
            args.channel,
            args.start_offset,
            args.count,
            s_data_samples,
            &copied_count);
    }
    if (err != ESP_OK) {
        xSemaphoreGive(s_data_response_lock);
        scpi_handler_write_esp_error(writer->write_text, context, err);
        return;
    }

    const size_t payload_size = copied_count * SCPI_HANDLER_DATA_RECORD_SIZE;
    const size_t digit_count = scpi_handler_decimal_digits(payload_size);
    int header_length = snprintf(
        (char *)s_data_response_buffer,
        SCPI_HANDLER_DATA_RESPONSE_BUFFER_SIZE,
        "#%zu%zu",
        digit_count,
        payload_size);
    if ((header_length <= 0) || ((size_t)header_length >= SCPI_HANDLER_DATA_RESPONSE_BUFFER_SIZE)) {
        xSemaphoreGive(s_data_response_lock);
        writer->write_text("ERR,\"Failed to encode binary block header\"");
        return;
    }

    size_t tx_length = (size_t)header_length;
    for (size_t i = 0; i < copied_count; ++i) {
        scpi_handler_write_u32_le(&s_data_response_buffer[tx_length], s_data_samples[i].time_ms);
        scpi_handler_write_u32_le(&s_data_response_buffer[tx_length + sizeof(uint32_t)], s_data_samples[i].value_u4);
        tx_length += SCPI_HANDLER_DATA_RECORD_SIZE;
    }

    ESP_LOGD(TAG, "%s records=%u payload=%u", context, (unsigned)copied_count, (unsigned)payload_size);
    writer->write_binary(s_data_response_buffer, tx_length);
    xSemaphoreGive(s_data_response_lock);
}

bool scpi_handler_handle_measure_command(
    const char *keyword,
    scpi_handler_parsed_command_t *parsed,
    const scpi_handler_response_writer_t *writer)
{
    if ((strcmp(keyword, "MEAS:ADC:RATE") == 0) ||
        (strcmp(keyword, "MEASURE:ADC:RATE") == 0)) {
        scpi_handler_handle_adc_rate_set(parsed, writer->write_text);
        return true;
    }

    if ((strcmp(keyword, "MEAS:ADC:RATE?") == 0) ||
        (strcmp(keyword, "MEASURE:ADC:RATE?") == 0)) {
        scpi_handler_handle_adc_rate_query(writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:VOLT:AVER:COUN") == 0) {
        scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_VOLTAGE, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:VOLT:AVER:COUN?") == 0) {
        scpi_handler_handle_average_count_query(MEASURE_KIND_VOLTAGE, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:VOLT?") == 0) {
        scpi_handler_handle_measure_query(parsed, MEASURE_KIND_VOLTAGE, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:VOLT:DATA?") == 0) {
        scpi_handler_handle_measure_data_query(parsed, MEASURE_KIND_VOLTAGE, writer);
        return true;
    }

    if (strcmp(keyword, "MEAS:CURR?") == 0) {
        scpi_handler_handle_measure_query(parsed, MEASURE_KIND_CURRENT, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:CURR:AVER:COUN") == 0) {
        scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_CURRENT, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:CURR:AVER:COUN?") == 0) {
        scpi_handler_handle_average_count_query(MEASURE_KIND_CURRENT, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:CURR:DATA?") == 0) {
        scpi_handler_handle_measure_data_query(parsed, MEASURE_KIND_CURRENT, writer);
        return true;
    }

    if (strcmp(keyword, "MEAS:POWER?") == 0) {
        scpi_handler_handle_measure_query(parsed, MEASURE_KIND_POWER, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:POWER:AVER:COUN") == 0) {
        scpi_handler_handle_average_count_set(parsed, MEASURE_KIND_POWER, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:POWER:AVER:COUN?") == 0) {
        scpi_handler_handle_average_count_query(MEASURE_KIND_POWER, writer->write_text);
        return true;
    }

    if (strcmp(keyword, "MEAS:POWER:DATA?") == 0) {
        scpi_handler_handle_measure_data_query(parsed, MEASURE_KIND_POWER, writer);
        return true;
    }

    return false;
}
