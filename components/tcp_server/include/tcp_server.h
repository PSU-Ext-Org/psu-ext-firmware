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
 * @file tcp_server.h
 * @brief TCP server transport for PSU-EXT over WiFi.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WiFi TCP server task.
 *
 * The server waits for WiFi connectivity, listens on TCP port `5025`, accepts
 * a single client at a time, and currently echoes received bytes back to the
 * connected peer. The task remains running so the transport can later be
 * upgraded from echo to SCPI handling without changing startup flow.
 *
 * @return
 * - `ESP_OK` if the server task was started
 * - `ESP_ERR_NO_MEM` if the task could not be created
 */
esp_err_t tcp_server_init(void);

#ifdef __cplusplus
}
#endif
