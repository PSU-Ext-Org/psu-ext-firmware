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

#include <stdint.h>

#include "esp_err.h"

/**
 * @file measure_svc_sampler.h
 * @brief Private control contract for the background input sampler.
 */

/** @brief Configure the rate used when the sampler task starts. */
esp_err_t measure_svc_sampler_set_rate_hz(uint32_t hz);
/** @brief Create the sampler task if it is not already running. */
esp_err_t measure_svc_sampler_start(void);
