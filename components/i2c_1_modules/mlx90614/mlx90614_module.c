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

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "i2c_1_modules_config.h"
#include "mlx90614.h"
#include "sdkconfig.h"

static const char *TAG = "pyrometer";
static mlx90614_state_t s_state;
static mlx90614_handle_t s_sensor;
static bool s_init_attempted;
static esp_err_t s_init_result;

esp_err_t mlx90614_module_register_commands(void)
{
    static bool registered;
    static const scpi_handler_command_t commands[] = {
        {"EXT:MEAS:TEMP?", mlx90614_query_temperature, &s_state},
        {"EXT:MEASURE:TEMP?", mlx90614_query_temperature, &s_state},
        {"EXT:MEAS:TEMPERATURE?", mlx90614_query_temperature, &s_state},
        {"EXT:MEASURE:TEMPERATURE?", mlx90614_query_temperature, &s_state},
        {"EXT:MEAS:TEMP:DATA?", mlx90614_query_history, &s_state},
        {"EXT:MEASURE:TEMP:DATA?", mlx90614_query_history, &s_state},
        {"EXT:MEAS:TEMPERATURE:DATA?", mlx90614_query_history, &s_state},
        {"EXT:MEASURE:TEMPERATURE:DATA?", mlx90614_query_history, &s_state},
    };
    if (registered) {
        return ESP_OK;
    }
    /* Publish immutable lock handles before command registration makes this
     * state reachable from concurrent transport callbacks. */
    if (s_state.history_lock == NULL) {
        s_state.history_lock = xSemaphoreCreateMutex();
    }
    if (s_state.response_lock == NULL) {
        s_state.response_lock = xSemaphoreCreateMutex();
    }
    if (s_state.history_lock == NULL || s_state.response_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = scpi_handler_register_commands(commands, sizeof(commands) / sizeof(commands[0]));
    registered = err == ESP_OK;
    return err;
}

static void acquire_temperature(void *context)
{
    (void)context;
    /* Results become valid 250 ms after sensor power-up. Waiting here is
     * conservative even when firmware startup has already exceeded that time. */
    vTaskDelay(pdMS_TO_TICKS(250));
    TickType_t last_wake = xTaskGetTickCount();
    bool failed = false;
    for (;;) {
        uint16_t raw = 0;
        esp_err_t err = mlx90614_read(s_sensor, 0x07, &raw);
        err = mlx90614_store_result(&s_state, err, raw, (uint32_t)(esp_timer_get_time() / 1000));
        if (err != ESP_OK && !failed) {
            ESP_LOGW(TAG, "Temperature unavailable: %s", esp_err_to_name(err));
        } else if (err == ESP_OK && failed) {
            ESP_LOGI(TAG, "Temperature readings recovered");
        }
        failed = err != ESP_OK;
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_PSUEXT_MLX90614_POLL_MS));
    }
}

esp_err_t mlx90614_module_init(i2c_master_bus_handle_t bus)
{
    if (s_init_attempted) {
        return s_init_result;
    }
    s_init_attempted = true;
    s_init_result = mlx90614_module_register_commands();
    if (s_init_result != ESP_OK) {
        return s_init_result;
    }
    s_init_result = ESP_ERR_INVALID_ARG;
    if (bus == NULL) {
        return s_init_result;
    }
    s_init_result = i2c_master_probe(bus, MLX90614_DEFAULT_ADDRESS, 50);
    if (s_init_result != ESP_OK) {
        return s_init_result;
    }
    const mlx90614_config_t config = {
        .mlx90614_device = {
            .device_address = MLX90614_DEFAULT_ADDRESS,
            .scl_speed_hz = I2C_1_MODULES_CLOCK_HZ,
        },
    };
    s_init_result = mlx90614_init(bus, &config, &s_sensor);
    if (s_init_result != ESP_OK) {
        return s_init_result;
    }
    s_init_result = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_state.history_lock, portMAX_DELAY);
    s_state.history.samples = heap_caps_calloc(MLX90614_HISTORY_CAPACITY,
        sizeof(mlx90614_sample_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_state.response = heap_caps_malloc(MLX90614_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    xSemaphoreGive(s_state.history_lock);
    if (s_state.history.samples == NULL || s_state.response == NULL) {
        goto fail;
    }
    /* Hold the lock until task creation succeeds, preventing an early task
     * read or SCPI query from observing a partially initialized service. */
    xSemaphoreTake(s_state.history_lock, portMAX_DELAY);
    BaseType_t created = xTaskCreate(acquire_temperature, "mlx90614", 4096, NULL,
                                    tskIDLE_PRIORITY + 1, NULL);
    s_state.ready = created == pdPASS;
    xSemaphoreGive(s_state.history_lock);
    if (created != pdPASS) {
        goto fail;
    }
    s_init_result = ESP_OK;
    return ESP_OK;

fail:
    /* ready remains false: callbacks cannot access these buffers. */
    xSemaphoreTake(s_state.history_lock, portMAX_DELAY);
    heap_caps_free(s_state.history.samples);
    heap_caps_free(s_state.response);
    s_state.history.samples = NULL;
    s_state.response = NULL;
    xSemaphoreGive(s_state.history_lock);
    esp_err_t cleanup_err = mlx90614_deinit(s_sensor);
    if (cleanup_err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor cleanup failed: %s", esp_err_to_name(cleanup_err));
    }
    s_sensor = NULL;
    return s_init_result;
}
