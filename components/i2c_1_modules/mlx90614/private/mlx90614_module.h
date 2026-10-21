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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scpi_handler.h"

enum {
    MLX90614_HISTORY_CAPACITY = 1000,
    MLX90614_RECORD_BYTES = 8,
    MLX90614_RESPONSE_BYTES = 16 + MLX90614_HISTORY_CAPACITY * MLX90614_RECORD_BYTES,
};

/* Signed Celsius x 10000; timestamps wrap like the existing measurement API. */
typedef struct {
    uint32_t time_ms;
    int32_t temperature_c4;
} mlx90614_sample_t;

/* Caller serializes ring operations. Samples are owned by the optional module
 * and allocated only in PSRAM. Offsets count from the oldest retained sample. */
typedef struct {
    mlx90614_sample_t *samples;
    size_t write_index;
    size_t count;
    bool latest_valid;
} mlx90614_history_t;

typedef struct {
    mlx90614_history_t history;
    SemaphoreHandle_t history_lock;
    SemaphoreHandle_t response_lock;
    uint8_t *response;
    bool ready;  /* Protected by history_lock, together with history. */
} mlx90614_state_t;

bool mlx90614_decode_temperature(uint16_t raw, int32_t *temperature_c4);
/* Apply one acquisition result under history_lock; errors preserve history
 * while invalidating the scalar until the next successful sample. */
esp_err_t mlx90614_store_result(mlx90614_state_t *state, esp_err_t result,
                               uint16_t raw, uint32_t time_ms);
void mlx90614_history_append(mlx90614_history_t *history, mlx90614_sample_t sample);
const mlx90614_sample_t *mlx90614_history_at(const mlx90614_history_t *history, size_t offset);
void mlx90614_query_temperature(const char *argument,
                               const scpi_handler_response_writer_t *writer, void *context);
void mlx90614_query_history(const char *argument,
                           const scpi_handler_response_writer_t *writer, void *context);

/* Startup-only registration, before probing the device; cached until reboot. */
esp_err_t mlx90614_module_register_commands(void);
/* Borrows the manager-owned bus. Startup failures are cached until reboot. */
esp_err_t mlx90614_module_init(i2c_master_bus_handle_t bus);
