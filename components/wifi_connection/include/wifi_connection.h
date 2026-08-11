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
 * @file wifi_connection.h
 * @brief Runtime WiFi station connection management for PSU-EXT.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the STA interface and try to connect using stored credentials.
 *
 * If both SSID and password are present in `wifi_manager`, the module starts
 * the station interface and retries failed connects up to three times before
 * giving up for the current startup. Normal connection failure does not return
 * an error; only infrastructure setup failures do.
 *
 * @return
 * - `ESP_OK` if the connection manager was initialized
 * - An ESP-IDF error code if network stack, event, or WiFi driver setup fails
 */
esp_err_t wifi_connection_init(void);

#ifdef __cplusplus
}
#endif
