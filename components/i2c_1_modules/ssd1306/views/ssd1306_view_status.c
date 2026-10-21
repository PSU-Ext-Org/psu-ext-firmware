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

#include "measure_svc.h"
#include "measure_svc_samples.h"
#include "ssd1306_draw.h"
#include "tcp_server.h"
#include "usb_com.h"
#include "wifi_manager.h"

/*
 * Two 64-pixel columns, 8-pixel row spacing, small 4 x 5 glyphs:
 *   left: USB host / CDC open / gap / Wi-Fi DHCP / TCP session / credentials.
 *   right: ADC rate / voltage average / current average / power average.
 * DHCP means an assigned IP; CDC open means host DTR is asserted.
 * ADC SPS counts all conversions; averaging values count samples per kind.
 * Keep each label and value within 16 character cells. ---- means unavailable.
 */
enum {
    STATUS_COLUMN_WIDTH = SSD1306_WIDTH / 2,
    STATUS_ROW_HEIGHT = 8,
};

static const char *connection_state(bool valid, bool connected)
{
    if (!valid) {
        return "----";
    }
    return connected ? "YES" : "NO";
}

static void draw_status_row(ssd1306_frame_t frame, unsigned row,
                            const char *label, const char *value)
{
    char text[24];
    snprintf(text, sizeof(text), "%s:%s", label, value);
    ssd1306_draw_text(frame, 0, row * STATUS_ROW_HEIGHT, 1, STATUS_COLUMN_WIDTH, text);
}

void ssd1306_render_status(const ssd1306_status_snapshot_t *data, ssd1306_frame_t frame)
{
    /* Connection states are ordered top to bottom to match the screen. */
    draw_status_row(frame, 0, "USB HOST", connection_state(true, data->usb_mounted));
    draw_status_row(frame, 1, "CDC OPEN", connection_state(true, data->usb_open));
    draw_status_row(frame, 3, "WIFI DHCP", connection_state(data->wifi_valid, data->wifi_connected));
    draw_status_row(frame, 4, "TCP ACTIVE", connection_state(true, data->tcp_active));
    draw_status_row(frame, 5, "CREDS", data->credentials_set ? "SET" : "NOT SET");
    char text[24];
    if (data->adc_valid) {
        snprintf(text, sizeof(text), "ADC:%u SPS", (unsigned)data->adc_rate);
    } else {
        snprintf(text, sizeof(text), "ADC:---- SPS");
    }
    ssd1306_draw_text(frame, STATUS_COLUMN_WIDTH, 0, 1, STATUS_COLUMN_WIDTH, text);
    const char kinds[] = "VIP";
    for (unsigned i = 0; i < 3; ++i) {
        if (data->average_valid[i]) {
            snprintf(text, sizeof(text), "AVG %c:%lu", kinds[i], (unsigned long)data->averages[i]);
        } else {
            snprintf(text, sizeof(text), "AVG %c:----", kinds[i]);
        }
        ssd1306_draw_text(frame, STATUS_COLUMN_WIDTH, (i + 1) * STATUS_ROW_HEIGHT, 1,
                          STATUS_COLUMN_WIDTH, text);
    }
}

static void draw_status(ssd1306_frame_t frame, void *context)
{
    (void)context;
    /* Keep service access out of the pure renderer so layouts can be tested
     * with synthetic states, including unavailable services and long values. */
    ssd1306_status_snapshot_t data = {0};
    wifi_manager_state_t wifi;
    data.wifi_valid = wifi_manager_get_state(&wifi) == ESP_OK;
    data.wifi_connected = data.wifi_valid && wifi.is_connected;
    data.credentials_set = wifi_manager_credentials_are_set();
    data.tcp_active = data.wifi_connected && tcp_server_session_is_active();
    const usb_com_status_t usb = usb_com_get_status();
    data.usb_mounted = usb.mounted;
    data.usb_open = usb.cdc_open;
    data.adc_valid = measure_svc_get_adc_data_rate_sps(&data.adc_rate) == ESP_OK;
    const measure_kind_t kinds[] = {MEASURE_KIND_VOLTAGE, MEASURE_KIND_CURRENT, MEASURE_KIND_POWER};
    for (unsigned i = 0; i < 3; ++i) {
        data.average_valid[i] = measure_svc_get_average_count(kinds[i], &data.averages[i]) == ESP_OK;
    }
    ssd1306_render_status(&data, frame);
}

esp_err_t ssd1306_register_status_view(void)
{
    const ssd1306_view_t view = {
        .id = 2,
        .draw = draw_status,
    };
    return ssd1306_views_register(&view);
}
