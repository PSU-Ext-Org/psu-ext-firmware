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
 * @file measure_svc_calibration_table.h
 * @brief Multi-point calibration table validation and fixed-point conversion.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "measure_svc_calibration.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One persistent interpolation knot in raw and physical u4 domains. */
typedef struct {
    int16_t raw_code; /**< Native signed ADS1115 conversion code. */
    uint32_t actual_u4; /**< Reference physical value multiplied by 10,000. */
    uint16_t pga_full_scale_mv; /**< PGA full-scale magnitude in millivolts. */
} measure_svc_cal_table_point_t;

/** @brief Persistent calibration knots plus a non-persistent lookup hint. */
typedef struct {
    uint8_t count; /**< Number of populated points. */
    measure_svc_cal_table_point_t points[MEASURE_SVC_CAL_MAX_POINTS]; /**< Ordered knots. */

    /* Runtime lookup hint. These fields must never be persisted. */
    uint8_t cached_segment; /**< Most recently selected segment index. */
    bool cache_valid; /**< Whether @ref cached_segment can be considered. */
} measure_svc_cal_table_t;

/**
 * @brief Validate the persistent contents of a calibration table.
 *
 * A valid table contains 2..32 points, at least two for every configured PGA.
 * Points for each PGA are strictly increasing in both raw and actual values;
 * points belonging to different PGAs may be interleaved.
 *
 * @param table Table whose persistent fields are checked.
 * @return `true` when the table can be used for interpolation.
 */
bool measure_svc_cal_table_validate(const measure_svc_cal_table_t *table);

/**
 * @brief Reset the runtime-only segment lookup hint.
 * @param table Table to update; a null pointer is ignored.
 */
void measure_svc_cal_table_invalidate_cache(measure_svc_cal_table_t *table);

/**
 * @brief Apply the matching PGA subset of a validated calibration table.
 *
 * The function performs piecewise-linear interpolation or endpoint-segment
 * extrapolation, rounds half away from zero, and saturates to uint32_t. A
 * successful call may update the table's runtime segment cache.
 *
 * @param table Calibration table and runtime lookup cache.
 * @param raw_code Native signed ADC conversion code.
 * @param actual_u4 Output receiving the calibrated value scaled by 10,000.
 * @return `true` on success, or `false` for a null/invalid table or output.
 */
bool measure_svc_cal_table_apply_u4(
    measure_svc_cal_table_t *table,
    int16_t raw_code,
    uint16_t pga_full_scale_mv,
    uint32_t *actual_u4);

#ifdef __cplusplus
}
#endif
