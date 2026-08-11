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
 * @file timebase_svc.c
 * @brief Runtime wall-clock to uptime mapping service.
 */

#include "timebase_svc.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t lock;
    bool initialized;
    timebase_svc_mapping_t base_mapping;
} timebase_svc_state_t;

static timebase_svc_state_t s_timebase_svc;
static const char *TAG = "timebase_svc";

uint32_t timebase_svc_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

esp_err_t timebase_svc_init(void)
{
    if (s_timebase_svc.initialized) {
        return ESP_OK;
    }

    if (s_timebase_svc.lock == NULL) {
        s_timebase_svc.lock = xSemaphoreCreateMutex();
        if (s_timebase_svc.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_timebase_svc.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_timebase_svc.base_mapping.unix_ms = 0ULL;
    s_timebase_svc.base_mapping.uptime_ms = 0U;
    s_timebase_svc.initialized = true;

    xSemaphoreGive(s_timebase_svc.lock);
    return ESP_OK;
}

esp_err_t timebase_svc_set_base_unix_ms(uint64_t unix_ms)
{
    ESP_RETURN_ON_FALSE(s_timebase_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");

    if (xSemaphoreTake(s_timebase_svc.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_timebase_svc.base_mapping.unix_ms = unix_ms;
    s_timebase_svc.base_mapping.uptime_ms = timebase_svc_uptime_ms();

    xSemaphoreGive(s_timebase_svc.lock);
    return ESP_OK;
}

esp_err_t timebase_svc_get_base_mapping(timebase_svc_mapping_t *mapping)
{
    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping is null");
    ESP_RETURN_ON_FALSE(s_timebase_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");

    if (xSemaphoreTake(s_timebase_svc.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *mapping = s_timebase_svc.base_mapping;

    xSemaphoreGive(s_timebase_svc.lock);
    return ESP_OK;
}

esp_err_t timebase_svc_get_current_unix_ms(uint64_t *unix_ms)
{
    timebase_svc_mapping_t mapping;
    uint32_t now_uptime_ms;

    ESP_RETURN_ON_FALSE(unix_ms != NULL, ESP_ERR_INVALID_ARG, TAG, "unix_ms is null");
    ESP_RETURN_ON_FALSE(s_timebase_svc.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");

    if (xSemaphoreTake(s_timebase_svc.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    mapping = s_timebase_svc.base_mapping;
    xSemaphoreGive(s_timebase_svc.lock);

    now_uptime_ms = timebase_svc_uptime_ms();
    *unix_ms = mapping.unix_ms + (uint64_t)(uint32_t)(now_uptime_ms - mapping.uptime_ms);
    return ESP_OK;
}
