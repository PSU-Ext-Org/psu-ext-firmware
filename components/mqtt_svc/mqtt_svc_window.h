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
 * @file mqtt_svc_window.h
 * @brief Telemetry window aggregation over measurement sample events.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "measure_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t sum_u4;
    uint32_t min_u4;
    uint32_t max_u4;
    uint32_t count;
} mqtt_svc_window_stat_t;

typedef struct {
    mqtt_svc_window_stat_t voltage;
    mqtt_svc_window_stat_t current;
    mqtt_svc_window_stat_t power;
} mqtt_svc_window_t;

typedef struct {
    uint32_t voltage_u4;
    uint32_t voltage_min_u4;
    uint32_t voltage_max_u4;
    uint32_t current_u4;
    uint32_t current_max_u4;
    uint32_t power_u4;
} mqtt_svc_telemetry_t;

/** @brief Clear all accumulated samples. */
void mqtt_svc_window_reset(mqtt_svc_window_t *window);

/** @brief Accumulate one calibrated CH1 sample of the given kind. */
void mqtt_svc_window_add(mqtt_svc_window_t *window, measure_kind_t kind, uint32_t value_u4);

/**
 * @brief Reduce the window to means and extremes.
 *
 * Means are rounded to the nearest u4 step. Power is the mean of per-sample
 * derived V*I values, so pulsed loads are represented correctly.
 *
 * @return `false` if the window has no voltage or no current samples.
 */
bool mqtt_svc_window_summarize(const mqtt_svc_window_t *window, mqtt_svc_telemetry_t *telemetry);

#ifdef __cplusplus
}
#endif
