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

#include "measure_svc_sampler.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "measure_svc.h"
#include "measure_svc_calibration_runtime.h"
#include "measure_svc_core.h"
#include "measure_svc_event_bus.h"

#define TASK_STACK_WORDS 4096U
#define TASK_PRIORITY 5U

static const char *TAG = "measure_sampler";
static TaskHandle_t s_task_handle;
static uint32_t s_sample_rate_hz = MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ;

static uint32_t clamp_rate(uint32_t hz)
{
    if (hz < MEASURE_SVC_MIN_SAMPLE_RATE_HZ) {
        return MEASURE_SVC_MIN_SAMPLE_RATE_HZ;
    }
    return hz > MEASURE_SVC_MAX_SAMPLE_RATE_HZ ? MEASURE_SVC_MAX_SAMPLE_RATE_HZ : hz;
}

static TickType_t sample_period(uint32_t hz)
{
    TickType_t period = pdMS_TO_TICKS(1000U / hz);
    return period == 0U ? 1U : period;
}

static void advance_due(TickType_t *due, TickType_t period)
{
    const TickType_t now = xTaskGetTickCount();
    do {
        *due += period;
    } while ((TickType_t)(now - *due) < (TickType_t)(portMAX_DELAY / 2U));
}

static void sample_input(measure_channel_t channel, measure_input_t input, measure_kind_t kind)
{
    uint32_t raw_u4;
    int16_t raw_code;
    uint32_t source_generation;
    esp_err_t err = measure_svc_core_read_raw_sample(
        input, MEASURE_KIND_VOLTAGE, &raw_u4, &raw_code, &source_generation);
    if (err == ESP_ERR_NOT_FINISHED) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sampling AIN%d failed: %s", (int)input, esp_err_to_name(err));
        return;
    }

    const measure_svc_sample_event_t event = {
        .kind = kind,
        .channel = channel,
        .physical_input = input,
        .time_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
        .raw_value_u4 = raw_u4,
        .raw_code = raw_code,
        .source_generation = source_generation,
        .value_u4 = measure_svc_calibration_apply_target_u4(kind, channel, raw_u4),
    };
    measure_svc_event_bus_publish(&event);
}

static void sampler_task(void *arg)
{
    (void)arg;
    const measure_svc_config_t config = *measure_svc_core_get_config();
    const uint32_t rate = clamp_rate(s_sample_rate_hz);
    const TickType_t period = sample_period(rate);
    TickType_t input_due = xTaskGetTickCount();
    TickType_t voltage_due = input_due;
    TickType_t current_due = input_due;

    ESP_LOGI(TAG, "starting sampler at %" PRIu32 " Hz per active input", rate);
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if ((config.input_voltage_input != MEASURE_INPUT_UNUSED) &&
            ((TickType_t)(now - input_due) < (TickType_t)(portMAX_DELAY / 2U))) {
            sample_input(MEASURE_CHANNEL_0, config.input_voltage_input, MEASURE_KIND_VOLTAGE);
            advance_due(&input_due, period);
        }
        if ((TickType_t)(now - voltage_due) < (TickType_t)(portMAX_DELAY / 2U)) {
            sample_input(MEASURE_CHANNEL_1, config.output_voltage_input, MEASURE_KIND_VOLTAGE);
            advance_due(&voltage_due, period);
        }
        if ((TickType_t)(now - current_due) < (TickType_t)(portMAX_DELAY / 2U)) {
            sample_input(MEASURE_CHANNEL_1, config.output_current_input, MEASURE_KIND_CURRENT);
            advance_due(&current_due, period);
        }

        const TickType_t delay_now = xTaskGetTickCount();
        TickType_t voltage_delay = ((TickType_t)(delay_now - voltage_due) < (TickType_t)(portMAX_DELAY / 2U))
            ? 1U : voltage_due - delay_now;
        TickType_t current_delay = ((TickType_t)(delay_now - current_due) < (TickType_t)(portMAX_DELAY / 2U))
            ? 1U : current_due - delay_now;
        TickType_t min_delay = voltage_delay < current_delay ? voltage_delay : current_delay;
        if (config.input_voltage_input != MEASURE_INPUT_UNUSED) {
            TickType_t input_delay = ((TickType_t)(delay_now - input_due) < (TickType_t)(portMAX_DELAY / 2U))
                ? 1U : input_due - delay_now;
            min_delay = min_delay < input_delay ? min_delay : input_delay;
        }
        vTaskDelay(min_delay);
    }
}

esp_err_t measure_svc_sampler_set_rate_hz(uint32_t hz)
{
    s_sample_rate_hz = clamp_rate(hz);
    return ESP_OK;
}

esp_err_t measure_svc_set_sample_rate_hz(uint32_t hz)
{
    return measure_svc_sampler_set_rate_hz(hz);
}

esp_err_t measure_svc_sampler_start(void)
{
    if (!measure_svc_core_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task_handle != NULL) {
        return ESP_OK;
    }
    if (xTaskCreate(sampler_task, "measure_sampler", TASK_STACK_WORDS, NULL,
        TASK_PRIORITY, &s_task_handle) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t measure_svc_start_sampling(void)
{
    return measure_svc_sampler_start();
}
