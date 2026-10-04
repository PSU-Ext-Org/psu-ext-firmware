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

/**
 * @file mqtt_svc_format.c
 * @brief Fixed-point text formatting for MQTT payloads.
 */

#include "mqtt_svc_format.h"

#include <stdio.h>

bool mqtt_svc_format_u4(uint32_t value_u4, char *text, size_t text_size)
{
    int written;

    if ((text == NULL) || (text_size == 0U)) {
        return false;
    }

    written = snprintf(
        text,
        text_size,
        "%lu.%04lu",
        (unsigned long)(value_u4 / 10000U),
        (unsigned long)(value_u4 % 10000U));
    return (written > 0) && ((size_t)written < text_size);
}
