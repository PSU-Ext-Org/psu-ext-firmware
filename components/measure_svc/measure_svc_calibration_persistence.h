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
 * @file measure_svc_calibration_persistence.h
 * @brief NVS loading, defaults, and storage for calibration tables.
 */

#pragma once

#include "esp_err.h"
#include "measure_types.h"
#include "measure_svc_calibration_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load one range-aware calibration target or create nominal defaults.
 *
 * Missing, malformed, old-format, or range-incompatible records produce the
 * target's nominal two-point-per-PGA default table.
 *
 * @param kind Calibration quantity selecting the storage key.
 * @param channel Logical channel selecting the storage key.
 * @param table Destination active table.
 * @return `ESP_OK` when a stored/default table is available, or an NVS error.
 */
esp_err_t measure_svc_cal_persistence_load(
    measure_kind_t kind,
    measure_channel_t channel,
    measure_svc_cal_table_t *table);

/**
 * @brief Encode and commit one validated calibration table to NVS.
 * @param kind Calibration quantity selecting the storage key.
 * @param channel Logical channel selecting the storage key.
 * @param table Table to encode in the stable record format.
 * @return `ESP_OK` or an argument, encoding, or NVS error.
 */
esp_err_t measure_svc_cal_persistence_store(
    measure_kind_t kind,
    measure_channel_t channel,
    const measure_svc_cal_table_t *table);

#ifdef __cplusplus
}
#endif
