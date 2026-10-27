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

#include "esp_err.h"

/* The unit-test linker redirects every meas_cfg open, including service calls,
 * to this namespace. Never use production calibration keys through a real handle. */
#define TEST_MEASURE_NVS_NAMESPACE "test_meas_cfg"

/* Initialization errors are reported, never repaired by erasing device NVS. */
esp_err_t test_nvs_init(void);
/* Erases only the dedicated test namespace, never the NVS partition. */
esp_err_t test_nvs_reset_measurement(void);
