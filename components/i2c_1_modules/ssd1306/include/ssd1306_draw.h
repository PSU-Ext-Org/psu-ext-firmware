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

#include "ssd1306_views.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw clipped uppercase 3x5 text. Width includes four-pixel cells at scale 1.
 * Unsupported glyphs leave an empty cell; pixels are ORed into the frame.
 * The caller owns the framebuffer and must use a positive scale.
 */
void ssd1306_draw_text(ssd1306_frame_t frame, unsigned x, unsigned y,
                       unsigned scale, unsigned width, const char *text);

#ifdef __cplusplus
}
#endif
