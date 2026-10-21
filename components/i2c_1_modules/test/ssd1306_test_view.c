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
#include "ssd1306_views.h"

#include <stddef.h>

#include "unity.h"

/* This view deliberately owns its data instead of extending built-in snapshots. */
static uint8_t s_marker = 0x5a;

static void draw_test_view(ssd1306_frame_t frame, void *context)
{
    TEST_ASSERT_EQUAL_PTR(&s_marker, context);
    for (size_t i = 0; i < sizeof(ssd1306_frame_t); ++i) {
        TEST_ASSERT_EQUAL_HEX8(0, ((uint8_t *)frame)[i]);
    }
    frame[0][0] = *(const uint8_t *)context;
}

esp_err_t ssd1306_register_test_view(void)
{
    const ssd1306_view_t view = {
        .id = 17,
        .draw = draw_test_view,
        .context = &s_marker,
    };
    return ssd1306_views_register(&view);
}
