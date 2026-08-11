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
 * @file timebase_svc.h
 * @brief Runtime wall-clock to uptime mapping service.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t unix_ms;
    uint32_t uptime_ms;
} timebase_svc_mapping_t;

/**
 * @brief Initialize the runtime timebase mapping.
 *
 * The default boot mapping is Unix epoch zero at uptime zero.
 */
esp_err_t timebase_svc_init(void);

/**
 * @brief Set the current wall-clock base in Unix milliseconds.
 *
 * The service snapshots the current uptime when this function is called.
 */
esp_err_t timebase_svc_set_base_unix_ms(uint64_t unix_ms);

/**
 * @brief Return the stored wall-clock to uptime base mapping.
 */
esp_err_t timebase_svc_get_base_mapping(timebase_svc_mapping_t *mapping);

/**
 * @brief Return the current derived wall-clock in Unix milliseconds.
 */
esp_err_t timebase_svc_get_current_unix_ms(uint64_t *unix_ms);

/**
 * @brief Return the compact uptime timestamp used by measurement samples.
 */
uint32_t timebase_svc_uptime_ms(void);

#ifdef __cplusplus
}
#endif
