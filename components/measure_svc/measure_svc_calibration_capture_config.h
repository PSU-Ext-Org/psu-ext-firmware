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

#include "freertos/FreeRTOS.h"

/**
 * @file measure_svc_calibration_capture_config.h
 * @brief Private compile-time policy for fresh calibration capture windows.
 */

#define MEASURE_SVC_CAL_CAPTURE_DISCARD_SAMPLES 2U /**< Fresh samples ignored per attempt. */
#define MEASURE_SVC_CAL_CAPTURE_WINDOW_SAMPLES 16U /**< Native codes averaged per attempt. */
#define MEASURE_SVC_CAL_STABILITY_P2P_MAX_CODES 16 /**< Largest accepted code spread. */
#define MEASURE_SVC_CAL_CAPTURE_TIMEOUT_TICKS pdMS_TO_TICKS(5000U) /**< Shared retry deadline. */
