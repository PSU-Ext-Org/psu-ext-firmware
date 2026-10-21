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
#include "ssd1306_views_internal.h"
#include "ssd1306_builtin_views.h"
#include "ssd1306_refresh.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "scpi_handler.h"
#include "unity.h"

static char s_response[96];
static void capture(const char *text)
{
    strlcpy(s_response, text, sizeof(s_response));
}

static void run_command(const char *text)
{
    char command[96];
    const scpi_handler_response_writer_t writer = {.write_text = capture};
    s_response[0] = '\0';
    strlcpy(command, text, sizeof(command));
    scpi_handler_handle_command(command, &writer);
}

esp_err_t ssd1306_register_test_view(void);

static void prepare_view(unsigned view)
{
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_register_scpi_commands());
    static bool test_view_registered;
    if (!test_view_registered) {
        TEST_ASSERT_EQUAL(ESP_OK, ssd1306_register_test_view());
        test_view_registered = true;
    }
    ssd1306_views_set_available(true);
    run_command(view == 1 ? "EXT:DISPL:VIEW 1" : "EXT:DISPL:VIEW 2");
    TEST_ASSERT_EQUAL_STRING("", s_response);
}

/* Simulate edge arrival and deadline expiry without GPIO hardware. */
static void advance_button(ssd1306_button_t *button, bool pressed, uint32_t now_ms)
{
    if (pressed != button->candidate_pressed) {
        ssd1306_button_edge(button, pressed, now_ms);
    }
    ssd1306_button_debounce(button, now_ms);
}

TEST_CASE("display commands validate arguments and report availability", "[display][scpi]")
{
    prepare_view(1);
    run_command("ext:display:view 2");
    TEST_ASSERT_EQUAL_STRING("", s_response);
    run_command("EXT:DISPL:VIEW?");
    TEST_ASSERT_EQUAL_STRING("2", s_response);
    run_command("EXT:DISPL:VIEW 1");
    run_command("EXT:DISPLAY:VIEW?");
    TEST_ASSERT_EQUAL_STRING("1", s_response);
    const char *invalid[] = {"", "0", "3", "-1", "1.0", "1,2", "1 2", "1x", "01", "+1", "42949672960"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char command[80];
        snprintf(command, sizeof(command), "EXT:DISPL:VIEW %s", invalid[i]);
        run_command(command);
        TEST_ASSERT_NOT_NULL(strstr(s_response, "ERR,"));
        TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    }
    run_command("EXT:DISPL:VIEW? 1");
    TEST_ASSERT_NOT_NULL(strstr(s_response, "ERR,"));
    ssd1306_views_set_available(false);
    run_command("EXT:DISPL:VIEW?");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Display unavailable\"", s_response);
    run_command("EXT:DISPLAY:VIEW 2");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Display unavailable\"", s_response);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
}

TEST_CASE("BOOT debounce ignores bounce holds and startup press", "[display][button]")
{
    prepare_view(1);
    ssd1306_button_t button;
    ssd1306_button_init(&button, true, 0);
    advance_button(&button, true, 100);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    advance_button(&button, false, 110);
    advance_button(&button, false, 150);
    advance_button(&button, true, 160);
    advance_button(&button, false, 180);
    advance_button(&button, true, 190);
    advance_button(&button, true, 229);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    advance_button(&button, true, 230);
    TEST_ASSERT_EQUAL(2, ssd1306_views_get_selected());
    advance_button(&button, true, 1000);
    TEST_ASSERT_EQUAL(2, ssd1306_views_get_selected());
    advance_button(&button, false, 1010);
    advance_button(&button, false, 1050);
    advance_button(&button, true, 1060);
    advance_button(&button, true, 1100);
    TEST_ASSERT_EQUAL(17, ssd1306_views_get_selected());
    /* Noncontiguous IDs wrap in registration order. */
    advance_button(&button, false, 1110);
    advance_button(&button, false, 1150);
    advance_button(&button, true, 1160);
    advance_button(&button, true, 1200);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    /* The next press cycles from the value selected by SCPI. */
    run_command("EXT:DISPL:VIEW 2");
    advance_button(&button, false, 1210);
    advance_button(&button, false, 1250);
    advance_button(&button, true, 1260);
    advance_button(&button, true, 1300);
    TEST_ASSERT_EQUAL(17, ssd1306_views_get_selected());
    ssd1306_views_set_available(false);
}

TEST_CASE("BOOT debounce handles timer wraparound", "[display][button]")
{
    prepare_view(1);
    ssd1306_button_t button;
    ssd1306_button_init(&button, false, UINT32_MAX - 30);
    advance_button(&button, true, UINT32_MAX - 20);
    advance_button(&button, true, 18);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    advance_button(&button, true, 19);
    TEST_ASSERT_EQUAL(2, ssd1306_views_get_selected());
    ssd1306_views_set_available(false);
}

TEST_CASE("captured bounce resets debounce even when the latest level repeats", "[display][button]")
{
    prepare_view(1);
    ssd1306_button_t button;
    ssd1306_button_init(&button, false, 0);
    ssd1306_button_edge(&button, true, 10);
    /* Multiple ISR edges can arrive while a display frame is transmitting. */
    ssd1306_button_edge(&button, false, 20);
    ssd1306_button_edge(&button, true, 30);
    ssd1306_button_debounce(&button, 69);
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    ssd1306_button_debounce(&button, 70);
    TEST_ASSERT_EQUAL(2, ssd1306_views_get_selected());
    ssd1306_views_set_available(false);
}

/* Exercise the pure built-in layouts without reading hardware services. */
static void render_measurements(const ssd1306_measurements_snapshot_t *data, ssd1306_frame_t frame)
{
    memset(frame, 0, sizeof(ssd1306_frame_t));
    ssd1306_render_measurements(data, frame);
}

static void render_status(const ssd1306_status_snapshot_t *data, ssd1306_frame_t frame)
{
    memset(frame, 0, sizeof(ssd1306_frame_t));
    ssd1306_render_status(data, frame);
}

static bool region_nonzero(ssd1306_frame_t frame, unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
    for (unsigned x = x0; x < x1; ++x) {
        for (unsigned y = y0; y < y1; ++y) {
            if (frame[y / 8][x] & (1U << (y % 8))) {
                return true;
            }
        }
    }
    return false;
}

TEST_CASE("display renderer fits maximum readings and clears complete frames", "[display][render]")
{
    struct {
        uint32_t before;
        ssd1306_frame_t frame;
        uint32_t after;
    } guarded = {.before = 0x12345678, .after = 0x87654321};
    ssd1306_measurements_snapshot_t data = {0};
    data.voltage = (ssd1306_reading_t){UINT32_MAX, true};
    data.current = data.power = data.input_voltage = data.voltage;
    data.relay_valid = true;
    render_measurements(&data, guarded.frame);
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 112, 120, 0, 10));
    TEST_ASSERT_FALSE(region_nonzero(guarded.frame, 60, 96, 0, 10));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 0, 64, 44, 49));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 0, 128, 58, 63));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 104, 128, 14, 24));
    ssd1306_status_snapshot_t status = {.adc_valid = true, .wifi_valid = true};
    status.adc_rate = UINT16_MAX;
    for (unsigned i = 0; i < 3; ++i) {
        status.average_valid[i] = true;
        status.averages[i] = UINT32_MAX;
    }
    memset(guarded.frame, 0xff, sizeof(guarded.frame));
    render_status(&status, guarded.frame);
    TEST_ASSERT_FALSE(region_nonzero(guarded.frame, 0, 128, 45, 64));
    TEST_ASSERT_FALSE(region_nonzero(guarded.frame, 0, 64, 16, 24));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 124, 127, 8, 13));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 64, 128, 16, 21));
    TEST_ASSERT_TRUE(region_nonzero(guarded.frame, 64, 128, 24, 29));
    TEST_ASSERT_FALSE(region_nonzero(guarded.frame, 64, 128, 32, 64));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, guarded.before);
    TEST_ASSERT_EQUAL_HEX32(0x87654321, guarded.after);
}

static void concurrent_selector(void *argument)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)argument;
    const scpi_handler_response_writer_t writer = {.write_text = capture};
    for (unsigned i = 0; i < 100; ++i) {
        char command[] = "EXT:DISPL:VIEW 2";
        scpi_handler_handle_command(command, &writer);
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

TEST_CASE("SCPI and button selection remain valid across tasks", "[display][concurrency]")
{
    prepare_view(1);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(done);
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(concurrent_selector, "view_test", 4096, done, 4, NULL));
    ssd1306_button_t button;
    ssd1306_button_init(&button, false, 0);
    for (unsigned i = 0; i < 100; ++i) {
        uint32_t start = i * 100;
        advance_button(&button, true, start);
        advance_button(&button, true, start + 40);
        advance_button(&button, false, start + 50);
        advance_button(&button, false, start + 90);
        unsigned view = ssd1306_views_get_selected();
        TEST_ASSERT_TRUE(view == 1 || view == 2 || view == 17);
    }
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(5000)));
    vSemaphoreDelete(done);
    ssd1306_views_set_available(false);
}

TEST_CASE("protection limits start directly after fixed labels without units", "[display][render]")
{
    ssd1306_measurements_snapshot_t data = {0};
    data.ovp_limit = (ssd1306_reading_t){1, true};
    data.ocp_limit = (ssd1306_reading_t){25000, true};
    data.ovp_trip_valid = data.ocp_trip_valid = true;
    ssd1306_frame_t before, after;
    render_measurements(&data, before);
    data.ovp_limit.value_u4 = data.ocp_limit.value_u4 = UINT32_MAX;
    data.ovp_tripped = data.ocp_tripped = true;
    render_measurements(&data, after);
    for (unsigned column = 0; column < 2; ++column) {
        unsigned x = column * 64;
        /* Limits and status labels remain at the same coordinates. */
        for (unsigned y = 51; y < 63; ++y) {
            for (unsigned dx = 0; dx < 16; ++dx) {
                uint8_t mask = (uint8_t)(1U << (y % 8));
                TEST_ASSERT_EQUAL(before[y / 8][x + dx] & mask, after[y / 8][x + dx] & mask);
            }
        }
        TEST_ASSERT_TRUE(region_nonzero(before, x + 16, x + 20, 51, 56));
        TEST_ASSERT_TRUE(region_nonzero(after, x + 16, x + 20, 51, 56));
        /* Maximum uint32_t u4 needs eleven cells, leaving the last one empty. */
        TEST_ASSERT_FALSE(region_nonzero(after, x + 60, x + 64, 51, 56));
    }
    /* Clearing trips must clear all pixels left by the longer TRIP text. */
    data.ovp_tripped = data.ocp_tripped = false;
    render_measurements(&data, after);
    TEST_ASSERT_FALSE(region_nonzero(after, 24, 32, 58, 63));
    TEST_ASSERT_FALSE(region_nonzero(after, 88, 96, 58, 63));
}

static void unused_view_draw(ssd1306_frame_t frame, void *context)
{
    (void)frame;
    (void)context;
}

TEST_CASE("display registry rejects invalid and duplicate views without changing selection", "[display][registry]")
{
    prepare_view(1);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssd1306_views_register(NULL));
    ssd1306_view_t view = {.id = 0, .draw = unused_view_draw};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssd1306_views_register(&view));
    view.id = 25;
    view.draw = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssd1306_views_register(&view));
    view.id = 1;
    view.draw = unused_view_draw;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ssd1306_views_register(&view));
    TEST_ASSERT_EQUAL(1, ssd1306_views_get_selected());
    ssd1306_views_set_available(false);
}

TEST_CASE("registered view receives context and a cleared frame", "[display][registry]")
{
    prepare_view(1);
    run_command("EXT:DISPL:VIEW 17");
    run_command("EXT:DISPL:VIEW?");
    TEST_ASSERT_EQUAL_STRING("17", s_response);
    ssd1306_frame_t frame;
    memset(frame, 0xff, sizeof(frame));
    ssd1306_views_render(17, frame);
    TEST_ASSERT_EQUAL_HEX8(0x5a, frame[0][0]);
    for (size_t i = 1; i < sizeof(frame); ++i) {
        TEST_ASSERT_EQUAL_HEX8(0, ((uint8_t *)frame)[i]);
    }
    ssd1306_views_render(25, frame);
    TEST_ASSERT_EQUAL_HEX8(0, frame[0][0]);
    ssd1306_views_set_available(false);
}

static unsigned s_page_writes;
static int s_failed_page;

static esp_err_t capture_page(uint8_t page, uint8_t column,
                              const uint8_t *pixels, size_t width)
{
    TEST_ASSERT_EQUAL(0, column);
    TEST_ASSERT_NOT_NULL(pixels);
    TEST_ASSERT_EQUAL(SSD1306_WIDTH, width);
    ++s_page_writes;
    return page == s_failed_page ? ESP_FAIL : ESP_OK;
}

TEST_CASE("page cache sends first frame changes and erasures but never commits failed writes", "[display][refresh]")
{
    ssd1306_page_cache_t cache = {0};
    ssd1306_frame_t frame = {0};
    s_failed_page = -1;
    s_page_writes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(SSD1306_PAGES, s_page_writes);
    s_page_writes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(0, s_page_writes);

    frame[2][17] = 0xff;
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(1, s_page_writes);
    /* A cleared new view must send the zeroes that erase the previous view. */
    memset(frame, 0, sizeof(frame));
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(2, s_page_writes);

    frame[3][0] = 1;
    frame[4][0] = 1;
    s_failed_page = 3;
    s_page_writes = 0;
    TEST_ASSERT_EQUAL(ESP_FAIL, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(1, s_page_writes);
    TEST_ASSERT_EQUAL(0, cache.previous[3][0]);
    TEST_ASSERT_EQUAL(0, cache.previous[4][0]);
    /* The helper permits retry; production stops on this error as before. */
    s_failed_page = -1;
    s_page_writes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ssd1306_refresh_pages(&cache, frame, capture_page));
    TEST_ASSERT_EQUAL(2, s_page_writes);
}
