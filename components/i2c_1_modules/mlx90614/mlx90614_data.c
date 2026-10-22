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

bool mlx90614_decode_temperature(uint16_t raw, int32_t *temperature_c4)
{
    if ((raw & 0x8000U) != 0 || temperature_c4 == NULL) {
        return false;
    }
    /* 0.02 Kelvin/count, converted exactly to signed Celsius x 10000. */
    *temperature_c4 = (int32_t)raw * 200 - 2731500;
    return true;
}

void mlx90614_history_append(mlx90614_history_t *history, mlx90614_sample_t sample)
{
    history->samples[history->write_index] = sample;
    history->write_index = (history->write_index + 1) % MLX90614_HISTORY_CAPACITY;
    if (history->count < MLX90614_HISTORY_CAPACITY) {
        ++history->count;
    }
    history->latest_valid = true;
}

const mlx90614_sample_t *mlx90614_history_at(const mlx90614_history_t *history, size_t offset)
{
    if (offset >= history->count) {
        return NULL;
    }
    const size_t oldest = (history->write_index + MLX90614_HISTORY_CAPACITY - history->count)
                          % MLX90614_HISTORY_CAPACITY;
    return &history->samples[(oldest + offset) % MLX90614_HISTORY_CAPACITY];
}

esp_err_t mlx90614_store_result(mlx90614_state_t *state, esp_err_t result,
                               uint16_t raw, uint32_t time_ms)
{
    int32_t temperature = 0;
    if (result == ESP_OK && !mlx90614_decode_temperature(raw, &temperature)) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreTake(state->history_lock, portMAX_DELAY);
    if (result == ESP_OK) {
        mlx90614_history_append(&state->history, (mlx90614_sample_t){time_ms, temperature});
    } else {
        state->history.latest_valid = false;
    }
    xSemaphoreGive(state->history_lock);
    return result;
}
