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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "scpi_handler.h"
#include "ssd1306_builtin_views.h"

static portMUX_TYPE s_view_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_selected_view;
static ssd1306_view_t s_views[SSD1306_MAX_VIEWS];
static size_t s_view_count;
static bool s_available;
static bool s_registered;

void ssd1306_views_set_available(bool available)
{
    portENTER_CRITICAL(&s_view_lock);
    s_available = available;
    portEXIT_CRITICAL(&s_view_lock);
}

unsigned ssd1306_views_get_selected(void)
{
    portENTER_CRITICAL(&s_view_lock);
    unsigned selected = s_selected_view;
    portEXIT_CRITICAL(&s_view_lock);
    return selected;
}

/* Accept canonical positive decimal IDs; reject signs, leading zeroes, and
 * overflow before consulting the registry. Zero is reserved for no selection. */
static unsigned parse_view_id(const char *argument)
{
    if (argument == NULL || argument[0] == '\0' || argument[0] == '0') {
        return 0;
    }
    unsigned id = 0;
    for (const char *digit = argument; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return 0;
        }
        const unsigned value = (unsigned)(*digit - '0');
        if (id > (UINT_MAX - value) / 10U) {
            return 0;
        }
        id = id * 10U + value;
    }
    return id;
}

static void select_view(const char *argument, const scpi_handler_response_writer_t *writer, void *context)
{
    (void)context;
    const unsigned id = parse_view_id(argument);
    portENTER_CRITICAL(&s_view_lock);
    bool available = s_available;
    bool found = false;
    for (size_t i = 0; i < s_view_count; ++i) {
        if (s_views[i].id == id) {
            found = true;
            break;
        }
    }
    if (available && found) {
        s_selected_view = id;
    }
    portEXIT_CRITICAL(&s_view_lock);
    if (!found) {
        writer->write_text("ERR,\"Expected a registered display view ID\"");
    } else if (!available) {
        writer->write_text("ERR,\"Display unavailable\"");
    }
}

static void query_view(const char *argument, const scpi_handler_response_writer_t *writer, void *context)
{
    (void)context;
    if (argument != NULL) {
        writer->write_text("ERR,\"Query takes no arguments\"");
        return;
    }
    portENTER_CRITICAL(&s_view_lock);
    bool available = s_available;
    unsigned selected = s_selected_view;
    portEXIT_CRITICAL(&s_view_lock);
    if (!available) {
        writer->write_text("ERR,\"Display unavailable\"");
        return;
    }
    char response[sizeof(unsigned) * 3 + 1];
    snprintf(response, sizeof(response), "%u", selected);
    writer->write_text(response);
}

esp_err_t ssd1306_register_scpi_commands(void)
{
    /* Startup-only registration; subsequent calls reuse the registered table. */
    static const scpi_handler_command_t commands[] = {
        {"EXT:(DISPL|DISPLAY):VIEW", select_view, NULL},
        {"EXT:(DISPL|DISPLAY):VIEW?", query_view, NULL},
    };
    if (s_registered) {
        return ESP_OK;
    }
    esp_err_t err = ssd1306_register_builtin_views();
    if (err != ESP_OK) {
        return err;
    }
    err = scpi_handler_register_patterns(commands, sizeof(commands) / sizeof(commands[0]));
    if (err == ESP_OK) {
        s_registered = true;
    }
    return err;
}

void ssd1306_button_init(ssd1306_button_t *button, bool pressed, uint32_t now_ms)
{
    *button = (ssd1306_button_t) {
        .candidate_pressed = pressed,
        .stable_pressed = pressed,
        .armed = !pressed,
        .candidate_since_ms = now_ms,
    };
}

void ssd1306_button_edge(ssd1306_button_t *button, bool pressed, uint32_t edge_ms)
{
    button->candidate_pressed = pressed;
    button->candidate_since_ms = edge_ms;
}

void ssd1306_button_debounce(ssd1306_button_t *button, uint32_t now_ms)
{
    const bool pressed = button->candidate_pressed;
    if (pressed == button->stable_pressed ||
        (uint32_t)(now_ms - button->candidate_since_ms) < SSD1306_BUTTON_DEBOUNCE_MS) {
        return;
    }
    button->stable_pressed = pressed;
    if (!pressed) {
        button->armed = true;
    } else if (button->armed) {
        button->armed = false;
        portENTER_CRITICAL(&s_view_lock);
        if (s_available) {
            for (size_t i = 0; i < s_view_count; ++i) {
                if (s_views[i].id == s_selected_view) {
                    s_selected_view = s_views[(i + 1) % s_view_count].id;
                    break;
                }
            }
        }
        portEXIT_CRITICAL(&s_view_lock);
    }
}

/* Copy descriptors under the lock; callbacks run only in the display task,
 * outside the lock, so service reads never block selectors or registration. */
esp_err_t ssd1306_views_register(const ssd1306_view_t *view)
{
    if (view == NULL || view->id == 0 || view->draw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_view_lock);
    for (size_t i = 0; i < s_view_count; ++i) {
        if (s_views[i].id == view->id) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
    }
    if (result == ESP_OK && s_view_count == SSD1306_MAX_VIEWS) {
        result = ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK) {
        s_views[s_view_count++] = *view;
        if (s_selected_view == 0) {
            s_selected_view = view->id;
        }
    }
    portEXIT_CRITICAL(&s_view_lock);
    return result;
}

void ssd1306_views_render(unsigned id, ssd1306_frame_t frame)
{
    ssd1306_view_t selected = {0};
    portENTER_CRITICAL(&s_view_lock);
    for (size_t i = 0; i < s_view_count; ++i) {
        if (s_views[i].id == id) {
            selected = s_views[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_view_lock);
    memset(frame, 0, sizeof(ssd1306_frame_t));
    if (selected.draw != NULL) {
        selected.draw(frame, selected.context);
    }
}
