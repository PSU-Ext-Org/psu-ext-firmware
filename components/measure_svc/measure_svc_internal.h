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
 * @file measure_svc_internal.h
 * @brief Component-private helpers shared by measurement service modules.
 */

#pragma once

#include "esp_err.h"
#include "measure_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t measure_svc_calibration_init(void);
uint32_t measure_svc_calibration_apply_target_u4(
    measure_kind_t kind,
    measure_channel_t channel,
    uint32_t raw_u4);
esp_err_t measure_svc_storage_register_listeners(void);
esp_err_t measure_svc_get_physical_input(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t *input);

#ifdef __cplusplus
}
#endif
