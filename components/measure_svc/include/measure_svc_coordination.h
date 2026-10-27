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
#include <stdbool.h>
#include "esp_err.h"

/* Holds the transaction-transition mutex until end. Lock order: coordination,
 * then protection or measurement. No calibration API may be called while held.
 * The active snapshot is valid for the entire guarded operation, including REL
 * updates and protection actions. Calibration clears REL under the same mutex. */
esp_err_t measure_svc_coordination_begin(bool *calibration_active);
void measure_svc_coordination_end(void);
