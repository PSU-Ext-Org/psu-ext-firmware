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
#include <stdint.h>

#include "ssd1306_views.h"

/* Built-in data model for pure layout tests; custom views own their data. */
typedef struct {
    uint32_t value_u4;  /* Fixed-point value scaled by 10000; use only if valid. */
    bool valid;
} ssd1306_reading_t;

typedef struct {
    ssd1306_reading_t voltage;
    ssd1306_reading_t current;
    ssd1306_reading_t power;
    ssd1306_reading_t input_voltage;
    bool relay;
    bool relay_valid;
    ssd1306_reading_t ovp_limit;
    ssd1306_reading_t ocp_limit;
    bool ovp_tripped;
    bool ocp_tripped;
    bool ovp_trip_valid;
    bool ocp_trip_valid;
} ssd1306_measurements_snapshot_t;

/* Connectivity and acquisition settings used only by the status view. */
typedef struct {
    bool wifi_connected;
    bool wifi_valid;
    bool tcp_active;
    bool credentials_set;
    bool usb_mounted;
    bool usb_open;
    uint16_t adc_rate;  /* ADC conversions per second, not per-channel rate. */
    bool adc_valid;
    uint32_t averages[3];  /* Voltage, current, power averaging windows. */
    bool average_valid[3];
} ssd1306_status_snapshot_t;

/* Startup catalog. Add new view registration calls in ssd1306_builtin_views.c. */
esp_err_t ssd1306_register_builtin_views(void);
esp_err_t ssd1306_register_measurements_view(void);
esp_err_t ssd1306_register_status_view(void);

/* Pure renderers for layout tests. Caller supplies a cleared framebuffer. */
void ssd1306_render_measurements(const ssd1306_measurements_snapshot_t *data, ssd1306_frame_t frame);
void ssd1306_render_status(const ssd1306_status_snapshot_t *data, ssd1306_frame_t frame);
