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

#include "measure_svc_power.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "measure_svc_core.h"
#include "measure_svc_event_bus.h"
#include "measure_svc_samples.h"

static const char *TAG = "measure_power";

typedef struct {
    bool valid;
    measure_svc_sample_event_t event;
} pending_input_t;

static pending_input_t s_voltage;
static pending_input_t s_current;

static uint32_t calculate_power_u4(uint32_t voltage_u4, uint32_t current_u4)
{
    return (uint32_t)((((uint64_t)voltage_u4 * current_u4) + 5000ULL) / 10000ULL);
}

static void emit_power(const measure_svc_sample_event_t *latest)
{
    const measure_svc_config_t *config = measure_svc_core_get_config();
    const measure_svc_sample_event_t event = {
        .kind = MEASURE_KIND_POWER,
        .channel = MEASURE_CHANNEL_1,
        .physical_input = config->output_current_input,
        .time_ms = latest->time_ms,
        .value_u4 = calculate_power_u4(s_voltage.event.value_u4, s_current.event.value_u4),
    };
    measure_svc_event_bus_publish(&event);
    s_voltage.valid = false;
    s_current.valid = false;
}

static void power_listener(const measure_svc_sample_event_t *event, void *context)
{
    (void)context;
    if ((event == NULL) || (event->channel != MEASURE_CHANNEL_1)) {
        return;
    }
    if (event->kind == MEASURE_KIND_VOLTAGE) {
        s_voltage = (pending_input_t){.valid = true, .event = *event};
        if (s_current.valid) {
            emit_power(event);
        }
    } else if (event->kind == MEASURE_KIND_CURRENT) {
        s_current = (pending_input_t){.valid = true, .event = *event};
        if (s_voltage.valid) {
            emit_power(event);
        }
    }
}

esp_err_t measure_svc_power_init(void)
{
    ESP_RETURN_ON_ERROR(measure_svc_register_sample_listener(
        MEASURE_KIND_VOLTAGE, power_listener, NULL), TAG, "registering voltage listener failed");
    return measure_svc_register_sample_listener(
        MEASURE_KIND_CURRENT, power_listener, NULL);
}
