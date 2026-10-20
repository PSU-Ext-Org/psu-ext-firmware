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

#include <stddef.h>

esp_err_t ssd1306_register_builtin_views(void)
{
    /* Explicit references keep each view linked from the component archive.
     * Registration order defines BOOT order and the first view is the default. */
    static esp_err_t (*const register_views[])(void) = {
        ssd1306_register_measurements_view,
        ssd1306_register_status_view,
    };
    /* Startup-only. Retain progress if one registration fails, so retrying does
     * not register earlier views twice. */
    static size_t registered_count;
    while (registered_count < sizeof(register_views) / sizeof(register_views[0])) {
        esp_err_t err = register_views[registered_count]();
        if (err != ESP_OK) {
            return err;
        }
        ++registered_count;
    }
    return ESP_OK;
}
