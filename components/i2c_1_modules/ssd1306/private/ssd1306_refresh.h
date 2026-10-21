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

/* Task-owned page cache. No I2C dependency so transfer policy can be tested. */
#pragma once

#include <stdbool.h>
#include <string.h>

#include "ssd1306_views.h"

typedef struct {
    ssd1306_frame_t previous;
    bool valid[SSD1306_PAGES];
} ssd1306_page_cache_t;

typedef esp_err_t (*ssd1306_page_writer_t)(uint8_t page, uint8_t column,
                                          const uint8_t *pixels, size_t width);

/* A zero-initialized cache forces a full first transfer. Commit each page only
 * after a successful write; return the first error to the display task, which
 * retains responsibility for marking the display unavailable and stopping. */
static inline esp_err_t ssd1306_refresh_pages(ssd1306_page_cache_t *cache,
                                             const ssd1306_frame_t frame,
                                             ssd1306_page_writer_t write_page)
{
    for (uint8_t page = 0; page < SSD1306_PAGES; ++page) {
        if (cache->valid[page] &&
            memcmp(cache->previous[page], frame[page], SSD1306_WIDTH) == 0) {
            continue;
        }
        esp_err_t err = write_page(page, 0, frame[page], SSD1306_WIDTH);
        if (err != ESP_OK) {
            return err;
        }
        memcpy(cache->previous[page], frame[page], SSD1306_WIDTH);
        cache->valid[page] = true;
    }
    return ESP_OK;
}
