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
#include "ssd1306_draw.h"

#include <string.h>

/* Three-column, five-row uppercase glyphs; each normal cell is four pixels. */
static const char GLYPH_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ :.-?";
static const uint8_t GLYPHS[][3] = {
    {0x1f,0x11,0x1f}, {0x12,0x1f,0x10}, {0x1d,0x15,0x17}, {0x15,0x15,0x1f},
    {0x07,0x04,0x1f}, {0x17,0x15,0x1d}, {0x1f,0x15,0x1d}, {0x01,0x1d,0x03},
    {0x1f,0x15,0x1f}, {0x17,0x15,0x1f},
    {0x1e,0x05,0x1e}, {0x1f,0x15,0x0a}, {0x1f,0x11,0x11}, {0x1f,0x11,0x0e},
    {0x1f,0x15,0x11}, {0x1f,0x05,0x01}, {0x1f,0x11,0x1d}, {0x1f,0x04,0x1f},
    {0x11,0x1f,0x11}, {0x08,0x10,0x0f}, {0x1f,0x04,0x1b}, {0x1f,0x10,0x10},
    {0x1f,0x02,0x1f}, {0x1f,0x01,0x1e}, {0x1f,0x11,0x1f}, {0x1f,0x05,0x07},
    {0x0f,0x09,0x1f}, {0x1f,0x05,0x1a}, {0x17,0x15,0x1d}, {0x01,0x1f,0x01},
    {0x1f,0x10,0x1f}, {0x0f,0x10,0x0f}, {0x1f,0x0c,0x1f}, {0x1b,0x04,0x1b},
    {0x03,0x1c,0x03}, {0x19,0x15,0x13},
    {0x00,0x00,0x00}, {0x00,0x0a,0x00}, {0x00,0x10,0x00}, {0x04,0x04,0x04},
    {0x01,0x15,0x03},
};
_Static_assert(sizeof(GLYPHS) / sizeof(GLYPHS[0]) == sizeof(GLYPH_CHARS) - 1, "Font mismatch");

void ssd1306_draw_text(ssd1306_frame_t frame, unsigned x, unsigned y, unsigned scale,
                      unsigned width, const char *text)
{
    for (unsigned cell = 0; text[cell] != '\0' && (cell + 1) * 4 * scale <= width; ++cell) {
        const char *glyph = strchr(GLYPH_CHARS, text[cell]);
        if (glyph == NULL) {
            continue;
        }
        for (unsigned col = 0; col < 3; ++col) {
            for (unsigned row = 0; row < 5; ++row) {
                if ((GLYPHS[glyph - GLYPH_CHARS][col] & (1U << row)) == 0) {
                    continue;
                }
                for (unsigned dx = 0; dx < scale; ++dx) {
                    for (unsigned dy = 0; dy < scale; ++dy) {
                        unsigned px = x + cell * 4 * scale + col * scale + dx;
                        unsigned py = y + row * scale + dy;
                        if (px < SSD1306_WIDTH && py < SSD1306_PAGES * 8) {
                            frame[py / 8][px] |= (uint8_t)(1U << (py % 8));
                        }
                    }
                }
            }
        }
    }
}
