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

enum { SSD1306_BUTTON_DEBOUNCE_MS = 40 };

typedef struct {
    bool candidate_pressed;
    bool stable_pressed;
    bool armed;
    uint32_t candidate_since_ms;
} ssd1306_button_t;

/* Module-owned registry and synchronized selection; no I2C operations. */
esp_err_t ssd1306_register_scpi_commands(void);
void ssd1306_views_set_available(bool available);
unsigned ssd1306_views_get_selected(void);
void ssd1306_button_init(ssd1306_button_t *button, bool pressed, uint32_t now_ms);
/* Feed captured GPIO edges, then complete debounce at the stability deadline. */
void ssd1306_button_edge(ssd1306_button_t *button, bool pressed, uint32_t edge_ms);
void ssd1306_button_debounce(ssd1306_button_t *button, uint32_t now_ms);
/* Display-task-only: clears the frame, then invokes the matching callback.
 * Unknown IDs produce a blank frame. */
void ssd1306_views_render(unsigned id, ssd1306_frame_t frame);
