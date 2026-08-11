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
 * @file status_led.h
 * @brief USB and WiFi status LED control for PSU-EXT.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t status_led_init(void);
void status_led_set_usb_connected(bool connected);
void status_led_set_usb_failed(bool failed);
void status_led_set_wifi_connected(bool connected);
void status_led_set_wifi_connecting(bool connecting);
void status_led_set_wifi_failed(bool failed);

#ifdef __cplusplus
}
#endif
