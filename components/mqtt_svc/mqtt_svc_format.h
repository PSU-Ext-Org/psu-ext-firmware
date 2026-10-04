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
 * @file mqtt_svc_format.h
 * @brief Fixed-point text formatting for MQTT payloads.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Longest `u4` text including terminator: `429496.7295`. */
#define MQTT_SVC_FORMAT_U4_SIZE 12U

/**
 * @brief Format a value scaled by 10,000 as `whole.ffff` without floats.
 *
 * @return `true` if the text fit in @p text.
 */
bool mqtt_svc_format_u4(uint32_t value_u4, char *text, size_t text_size);

#ifdef __cplusplus
}
#endif
