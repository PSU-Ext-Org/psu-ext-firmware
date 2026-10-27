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

#include "mlx90614_module.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

typedef struct {
    size_t count;
    size_t offset;
} history_args_t;

static void skip_space(const char **cursor)
{
    while (isspace((unsigned char)**cursor)) {
        ++*cursor;
    }
}

static bool parse_channel(const char **cursor)
{
    if (*cursor == NULL) {
        return false;
    }
    skip_space(cursor);
    if (toupper((unsigned char)(*cursor)[0]) == 'C' &&
        toupper((unsigned char)(*cursor)[1]) == 'H') {
        *cursor += 2;
    }
    if (**cursor != '1') {
        return false;
    }
    ++*cursor;
    skip_space(cursor);
    return **cursor == '\0' || **cursor == ',';
}

static bool parse_size(const char **cursor, size_t *value)
{
    skip_space(cursor);
    *value = 0;
    if (!isdigit((unsigned char)**cursor)) {
        return false;
    }
    while (isdigit((unsigned char)**cursor)) {
        unsigned digit = (unsigned)(**cursor - '0');
        if (*value > (SIZE_MAX - digit) / 10) {
            return false;
        }
        *value = *value * 10 + digit;
        ++*cursor;
    }
    skip_space(cursor);
    return true;
}

static bool parse_history_args(const char *argument, history_args_t *args)
{
    args->count = MLX90614_HISTORY_CAPACITY;
    args->offset = 0;
    if (!parse_channel(&argument)) {
        return false;
    }
    if (*argument == '\0') {
        return true;
    }
    ++argument;  /* Comma after channel. */
    if (!parse_size(&argument, &args->count)) {
        return false;
    }
    if (*argument == ',') {
        ++argument;
        if (!parse_size(&argument, &args->offset)) {
            return false;
        }
    }
    return *argument == '\0';
}

static void write_u32_le(uint8_t *target, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        target[i] = (uint8_t)(value >> (8 * i));
    }
}

void mlx90614_query_temperature(const char *argument,
                               const scpi_handler_response_writer_t *writer, void *context)
{
    if (!parse_channel(&argument) || *argument != '\0') {
        writer->write_text("ERR,\"Expected temperature channel 1|CH1\"");
        return;
    }
    mlx90614_state_t *state = context;
    if (state->history_lock == NULL) {
        writer->write_text("ERR,\"Temperature unavailable\"");
        return;
    }
    xSemaphoreTake(state->history_lock, portMAX_DELAY);
    const bool valid = state->ready && state->history.latest_valid && state->history.count > 0;
    int32_t value = 0;
    if (valid) {
        size_t count = state->history.count;
        if (count > CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT) {
            count = CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT;
        }
        int64_t sum = 0;
        for (size_t i = state->history.count - count; i < state->history.count; ++i) {
            sum += mlx90614_history_at(&state->history, i)->temperature_c4;
        }
        /* Round once to Celsius x 10000, with signed ties away from zero. */
        const int64_t half_count = (int64_t)count / 2;
        value = (int32_t)((sum + (sum < 0 ? -half_count : half_count)) / (int64_t)count);
    }
    xSemaphoreGive(state->history_lock);
    if (!valid) {
        writer->write_text("ERR,\"Temperature unavailable\"");
        return;
    }
    /* Widen before negating so even INT32_MIN is formatted without overflow. */
    const uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    char response[24];
    snprintf(response, sizeof(response), "%s%" PRIu32 ".%04" PRIu32,
             value < 0 ? "-" : "", magnitude / 10000, magnitude % 10000);
    writer->write_text(response);
}

void mlx90614_query_history(const char *argument,
                           const scpi_handler_response_writer_t *writer, void *context)
{
    history_args_t args;
    if (writer->write_binary == NULL) {
        writer->write_text("ERR,\"Binary response not supported\"");
        return;
    }
    if (!parse_history_args(argument, &args)) {
        writer->write_text("ERR,\"Expected DATA query 1|CH1[,count[,start_offset]]\"");
        return;
    }
    mlx90614_state_t *state = context;
    if (state->history_lock == NULL || state->response_lock == NULL) {
        writer->write_text("ERR,\"Temperature unavailable\"");
        return;
    }
    /* Response storage stays stable until the transport callback returns. The
     * acquisition task needs only history_lock and never takes response_lock. */
    if (xSemaphoreTake(state->response_lock, 0) != pdTRUE) {
        writer->write_text("ERR,\"Temperature history busy\"");
        return;
    }
    xSemaphoreTake(state->history_lock, portMAX_DELAY);
    if (!state->ready) {
        xSemaphoreGive(state->history_lock);
        xSemaphoreGive(state->response_lock);
        writer->write_text("ERR,\"Temperature unavailable\"");
        return;
    }
    size_t count = args.offset < state->history.count ? state->history.count - args.offset : 0;
    if (count > args.count) {
        count = args.count;
    }
    const size_t bytes = count * MLX90614_RECORD_BYTES;
    unsigned digits = 1;
    for (size_t remaining = bytes; remaining >= 10; remaining /= 10) {
        ++digits;
    }
    size_t length = (size_t)snprintf((char *)state->response, MLX90614_RESPONSE_BYTES,
                                    "#%u%zu", digits, bytes);
    for (size_t i = 0; i < count; ++i) {
        const mlx90614_sample_t *sample = mlx90614_history_at(&state->history, args.offset + i);
        write_u32_le(state->response + length, sample->time_ms);
        /* Conversion to unsigned preserves the two's-complement wire bits. */
        write_u32_le(state->response + length + 4, (uint32_t)sample->temperature_c4);
        length += MLX90614_RECORD_BYTES;
    }
    xSemaphoreGive(state->history_lock);
    writer->write_binary(state->response, length);
    xSemaphoreGive(state->response_lock);
}
