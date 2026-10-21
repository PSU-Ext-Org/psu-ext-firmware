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
#include "ssd1306_builtin_views.h"

#include <stdio.h>
#include <string.h>

#include "measure_svc.h"
#include "measure_svc_samples.h"
#include "output_ctrl.h"
#include "protection_svc.h"
#include "ssd1306_draw.h"

/*
 * Screen map (pixel coordinates, 128 x 64):
 *   y=0/14: CH1 voltage/current, large; relay occupies the right 32 pixels.
 *   y=28:   CH1 power, large, full width.
 *   y=44:   input voltage, small.
 *   y=51/58: OVP and OCP limits / trip states in two equal columns.
 * Small glyphs occupy 4 x 5 pixels; scale 2 occupies 8 x 10 pixels.
 * ---- means unavailable, never a measured zero. Keep four decimal places.
 */
enum {
    MEASUREMENT_WIDTH = 96,
    PROTECTION_COLUMN_WIDTH = SSD1306_WIDTH / 2,
    PROTECTION_LABEL_WIDTH = 16,
    PROTECTION_LIMIT_Y = 51,
    PROTECTION_STATE_Y = 58,
};

static ssd1306_reading_t read_measurement(measure_channel_t channel, measure_kind_t kind)
{
    ssd1306_reading_t reading = {0};
    reading.valid = measure_svc_read(channel, kind, &reading.value_u4) == ESP_OK;
    return reading;
}

static void draw_reading(ssd1306_frame_t frame, unsigned y, unsigned scale, unsigned width,
                         const char *label, char unit, ssd1306_reading_t reading)
{
    char text[32];
    if (reading.valid) {
        snprintf(text, sizeof(text), "%s:%lu.%04lu%c", label,
                 (unsigned long)(reading.value_u4 / 10000U),
                 (unsigned long)(reading.value_u4 % 10000U), unit);
    } else {
        snprintf(text, sizeof(text), "%s:----%c", label, unit);
    }
    /* Preserve every digit if an unusually large value needs the smaller font. */
    if (strlen(text) * 4U * scale > width) {
        scale = 1;
    }
    ssd1306_draw_text(frame, 0, y, scale, width, text);
}

static void draw_protection(ssd1306_frame_t frame, unsigned x, const char *label,
                            ssd1306_reading_t limit, bool trip_valid, bool tripped)
{
    /* Labels and value starts stay fixed in independent 64-pixel columns. */
    ssd1306_draw_text(frame, x, PROTECTION_LIMIT_Y, 1, PROTECTION_LABEL_WIDTH, label);
    char value[16];
    if (limit.valid) {
        snprintf(value, sizeof(value), "%lu.%04lu",
                 (unsigned long)(limit.value_u4 / 10000U),
                 (unsigned long)(limit.value_u4 % 10000U));
    } else {
        snprintf(value, sizeof(value), "----");
    }
    ssd1306_draw_text(frame, x + PROTECTION_LABEL_WIDTH, PROTECTION_LIMIT_Y, 1,
                      PROTECTION_COLUMN_WIDTH - PROTECTION_LABEL_WIDTH, value);
    ssd1306_draw_text(frame, x, PROTECTION_STATE_Y, 1, PROTECTION_LABEL_WIDTH, label);
    ssd1306_draw_text(frame, x + PROTECTION_LABEL_WIDTH, PROTECTION_STATE_Y, 1,
                      PROTECTION_COLUMN_WIDTH - PROTECTION_LABEL_WIDTH, !trip_valid ? "----" : (tripped ? "TRIP" : "OK"));
}

void ssd1306_render_measurements(const ssd1306_measurements_snapshot_t *data, ssd1306_frame_t frame)
{
    draw_reading(frame, 0, 2, MEASUREMENT_WIDTH, "V1", 'V', data->voltage);
    draw_reading(frame, 14, 2, MEASUREMENT_WIDTH, "I1", 'A', data->current);
    draw_reading(frame, 28, 2, SSD1306_WIDTH, "P1", 'W', data->power);
    draw_reading(frame, 44, 1, SSD1306_WIDTH, "VIN", 'V', data->input_voltage);
    ssd1306_draw_text(frame, 104, 0, 2, 24, "REL");
    const char *relay = !data->relay_valid ? "----" : (data->relay ? "ON" : "OFF");
    const unsigned relay_width = (unsigned)strlen(relay) * 8U;
    ssd1306_draw_text(frame, SSD1306_WIDTH - relay_width, 14, 2, relay_width, relay);
    draw_protection(frame, 0, "OVP:", data->ovp_limit, data->ovp_trip_valid, data->ovp_tripped);
    draw_protection(frame, PROTECTION_COLUMN_WIDTH, "OCP:", data->ocp_limit, data->ocp_trip_valid, data->ocp_tripped);
}

static void draw_measurements(ssd1306_frame_t frame, void *context)
{
    (void)context;
    /* Read services once per refresh; the renderer only consumes this snapshot.
     * Service reads are individually synchronized, not one atomic acquisition. */
    ssd1306_measurements_snapshot_t data = {0};
    data.voltage = read_measurement(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE);
    data.current = read_measurement(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT);
    data.power = read_measurement(MEASURE_CHANNEL_1, MEASURE_KIND_POWER);
    data.input_voltage = read_measurement(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE);
    data.relay_valid = output_ctrl_get(1, &data.relay) == ESP_OK;
    data.ovp_limit.valid = protection_svc_get_ovp_u4(
        PROTECTION_SVC_CHANNEL_CH1, &data.ovp_limit.value_u4) == ESP_OK;
    data.ocp_limit.valid = protection_svc_get_ocp_u4(
        PROTECTION_SVC_CHANNEL_CH1, &data.ocp_limit.value_u4) == ESP_OK;
    data.ovp_trip_valid = protection_svc_get_ovp_tripped(
        PROTECTION_SVC_CHANNEL_CH1, &data.ovp_tripped) == ESP_OK;
    data.ocp_trip_valid = protection_svc_get_ocp_tripped(
        PROTECTION_SVC_CHANNEL_CH1, &data.ocp_tripped) == ESP_OK;
    ssd1306_render_measurements(&data, frame);
}

esp_err_t ssd1306_register_measurements_view(void)
{
    const ssd1306_view_t view = {
        .id = 1,
        .draw = draw_measurements,
    };
    return ssd1306_views_register(&view);
}
