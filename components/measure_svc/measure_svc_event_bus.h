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
#include "measure_svc_samples.h"

/**
 * @file measure_svc_event_bus.h
 * @brief Private synchronous sample-event publication contract.
 */

/** @brief Initialize the listener registry and its lock. */
esp_err_t measure_svc_event_bus_init(void);
/**
 * @brief Publish an event synchronously to a snapshot of registered listeners.
 * @param event Event that remains valid for the duration of each callback.
 */
void measure_svc_event_bus_publish(const measure_svc_sample_event_t *event);
