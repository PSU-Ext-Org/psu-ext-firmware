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

#include "driver/i2c_master.h"
#include "esp_err.h"

/* Borrows the manager's bus. Called once at startup after sampling starts.
 * Returns the cached startup result on repeat calls; failures need a reboot.
 * Cleanup removes only the display device, never the shared bus.
 */
esp_err_t ssd1306_init(i2c_master_bus_handle_t bus);
