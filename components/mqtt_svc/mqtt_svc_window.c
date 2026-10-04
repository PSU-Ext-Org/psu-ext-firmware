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
 * @file mqtt_svc_window.c
 * @brief Telemetry window aggregation over measurement sample events.
 */

#include "mqtt_svc_window.h"

#include <stddef.h>
#include <string.h>

static void mqtt_svc_window_stat_add(mqtt_svc_window_stat_t *stat, uint32_t value_u4)
{
    if (stat->count == 0U) {
        stat->min_u4 = value_u4;
        stat->max_u4 = value_u4;
    } else {
        stat->min_u4 = value_u4 < stat->min_u4 ? value_u4 : stat->min_u4;
        stat->max_u4 = value_u4 > stat->max_u4 ? value_u4 : stat->max_u4;
    }

    stat->sum_u4 += value_u4;
    stat->count++;
}

static uint32_t mqtt_svc_window_stat_mean(const mqtt_svc_window_stat_t *stat)
{
    if (stat->count == 0U) {
        return 0U;
    }

    return (uint32_t)((stat->sum_u4 + (stat->count / 2U)) / stat->count);
}

void mqtt_svc_window_reset(mqtt_svc_window_t *window)
{
    if (window != NULL) {
        memset(window, 0, sizeof(*window));
    }
}

void mqtt_svc_window_add(mqtt_svc_window_t *window, measure_kind_t kind, uint32_t value_u4)
{
    if (window == NULL) {
        return;
    }

    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        mqtt_svc_window_stat_add(&window->voltage, value_u4);
        break;
    case MEASURE_KIND_CURRENT:
        mqtt_svc_window_stat_add(&window->current, value_u4);
        break;
    case MEASURE_KIND_POWER:
        mqtt_svc_window_stat_add(&window->power, value_u4);
        break;
    default:
        break;
    }
}

bool mqtt_svc_window_summarize(const mqtt_svc_window_t *window, mqtt_svc_telemetry_t *telemetry)
{
    if ((window == NULL) || (telemetry == NULL) ||
        (window->voltage.count == 0U) || (window->current.count == 0U)) {
        return false;
    }

    telemetry->voltage_u4 = mqtt_svc_window_stat_mean(&window->voltage);
    telemetry->voltage_min_u4 = window->voltage.min_u4;
    telemetry->voltage_max_u4 = window->voltage.max_u4;
    telemetry->current_u4 = mqtt_svc_window_stat_mean(&window->current);
    telemetry->current_max_u4 = window->current.max_u4;
    telemetry->power_u4 = mqtt_svc_window_stat_mean(&window->power);
    return true;
}
