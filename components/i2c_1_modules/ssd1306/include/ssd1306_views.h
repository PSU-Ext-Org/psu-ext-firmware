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

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SSD1306_WIDTH = 128,
    SSD1306_PAGES = 8,
    SSD1306_MAX_VIEWS = 8,
};
typedef uint8_t ssd1306_frame_t[SSD1306_PAGES][SSD1306_WIDTH];

/* Views receive a cleared task-owned framebuffer. Read services and draw only
 * from this callback; do not retain the framebuffer or access I2C directly.
 * Context is borrowed and must remain valid until reboot. */
typedef void (*ssd1306_view_draw_fn_t)(ssd1306_frame_t frame, void *context);
typedef struct {
    unsigned id;  /* Nonzero stable SCPI ID; need not be contiguous. */
    ssd1306_view_draw_fn_t draw;
    void *context;
} ssd1306_view_t;

/* Copies the descriptor without allocating memory. Safe alongside selection and
 * rendering; callbacks run outside the lock, on the display task only.
 * First registered view is the default; BOOT follows registration order.
 * Returns INVALID_ARG for zero ID/null callback, INVALID_STATE for a duplicate,
 * or NO_MEM when SSD1306_MAX_VIEWS is reached. No unregister until reboot.
 * Register built-ins first to preserve the default view and button order.
 */
esp_err_t ssd1306_views_register(const ssd1306_view_t *view);

#ifdef __cplusplus
}
#endif
