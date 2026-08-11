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
 * @file usb_com.h
 * @brief USB CDC ACM transport for SCPI-style command exchange.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the USB CDC command transport.
 *
 * Installs the TinyUSB device driver, configures a CDC ACM interface, and
 * enables line-based command processing over the native USB port.
 *
 * The function is idempotent: calling it multiple times after successful
 * initialization returns `ESP_OK` without reinitializing the driver.
 *
 * @return
 * - `ESP_OK` if the USB CDC service is ready
 * - An ESP-IDF error code if TinyUSB or CDC initialization fails
 */
esp_err_t usb_com_init(void);
