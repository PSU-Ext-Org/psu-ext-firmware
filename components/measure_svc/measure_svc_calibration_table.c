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
 * @file measure_svc_calibration_table.c
 * @brief Multi-point calibration table validation and fixed-point conversion.
 */

#include "measure_svc_calibration_table.h"

#include <limits.h>
#include <stddef.h>

#include "sdkconfig.h"
#include "measure_provider.h"

bool measure_svc_cal_table_validate(const measure_svc_cal_table_t *table)
{
    if ((table == NULL) ||
        (table->count < 2U) ||
        (table->count > MEASURE_SVC_CAL_MAX_POINTS)) {
        return false;
    }

    uint16_t configured[4];
    const size_t configured_count = measure_prov_ads1115_get_configured_pgas(
        configured, sizeof(configured) / sizeof(configured[0]));
    if ((configured_count == 0U) ||
        (configured_count > (sizeof(configured) / sizeof(configured[0])))) {
        return false;
    }

    for (uint8_t index = 0U; index < table->count; ++index) {
        if (!measure_prov_ads1115_is_pga_configured(
                table->points[index].pga_full_scale_mv)) {
            return false;
        }
    }

    for (size_t range = 0U; range < configured_count; ++range) {
        const measure_svc_cal_table_point_t *previous = NULL;
        uint8_t points_in_range = 0U;
        for (uint8_t index = 0U; index < table->count; ++index) {
            const measure_svc_cal_table_point_t *current = &table->points[index];
            if (current->pga_full_scale_mv != configured[range]) {
                continue;
            }
            if ((previous != NULL) &&
                ((current->raw_code_q16 <= previous->raw_code_q16) ||
                 (current->actual_u4 <= previous->actual_u4))) {
                return false;
            }
            previous = current;
            ++points_in_range;
        }
#if CONFIG_PSUEXT_CAL_REQUIRE_MIN_POINTS_PER_PGA
        if (points_in_range < CONFIG_PSUEXT_CAL_MIN_POINTS_PER_PGA) {
            return false;
        }
#endif
    }

    return true;
}

void measure_svc_cal_table_invalidate_cache(measure_svc_cal_table_t *table)
{
    if (table == NULL) {
        return;
    }

    table->cached_segment = 0U;
    table->cache_valid = false;
}

static bool measure_svc_cal_table_find_segment(
    measure_svc_cal_table_t *table,
    int16_t raw_code,
    uint16_t pga_full_scale_mv,
    uint8_t *first_index,
    uint8_t *second_index)
{
    uint8_t previous = UINT8_MAX;
    for (uint8_t index = 0U; index < table->count; ++index) {
        if (table->points[index].pga_full_scale_mv != pga_full_scale_mv) {
            continue;
        }
        if (previous == UINT8_MAX) {
            previous = index;
            continue;
        }
        if ((int32_t)raw_code * 65536 <= table->points[index].raw_code_q16) {
            *first_index = previous;
            *second_index = index;
            table->cached_segment = previous;
            table->cache_valid = true;
            return true;
        }
        *first_index = previous;
        *second_index = index;
        previous = index;
    }
    if ((previous != UINT8_MAX) && (*first_index != UINT8_MAX)) {
        table->cached_segment = *first_index;
        table->cache_valid = true;
        return true;
    }
    return false;
}

bool measure_svc_cal_table_apply_u4(
    measure_svc_cal_table_t *table,
    int16_t raw_code,
    uint16_t pga_full_scale_mv,
    uint32_t *actual_u4)
{
    if ((actual_u4 == NULL) || !measure_svc_cal_table_validate(table)) {
        return false;
    }

    uint8_t first = UINT8_MAX;
    uint8_t second = UINT8_MAX;
    if (!measure_svc_cal_table_find_segment(
            table, raw_code, pga_full_scale_mv, &first, &second)) {
        return false;
    }
    const measure_svc_cal_table_point_t *point0 = &table->points[first];
    const measure_svc_cal_table_point_t *point1 = &table->points[second];

    const int64_t x = (int64_t)raw_code * 65536LL;
    if (x == point0->raw_code_q16) {
        *actual_u4 = point0->actual_u4;
        return true;
    }
    if (x == point1->raw_code_q16) {
        *actual_u4 = point1->actual_u4;
        return true;
    }

    const int64_t raw_span = (int64_t)point1->raw_code_q16 - (int64_t)point0->raw_code_q16;
    const int64_t actual_span = (int64_t)point1->actual_u4 - (int64_t)point0->actual_u4;
    const int64_t raw_delta = x - (int64_t)point0->raw_code_q16;
    /* The two factors are bounded below 2^32.  Divide first and retain the
     * remainder, so no intermediate needs wider-than-64-bit arithmetic. */
    const uint64_t delta_magnitude = raw_delta < 0 ? (uint64_t)-raw_delta : (uint64_t)raw_delta;
    const uint64_t actual_magnitude = actual_span < 0 ? (uint64_t)-actual_span : (uint64_t)actual_span;
    const uint64_t denominator = (uint64_t)raw_span;
    const uint64_t whole = delta_magnitude / denominator;
    const uint64_t remainder = delta_magnitude % denominator;
    const uint64_t fractional = (remainder * actual_magnitude + denominator / 2U) / denominator;
    const uint64_t magnitude = whole * actual_magnitude + fractional;
    const bool negative = (raw_delta < 0) != (actual_span < 0);
    if (negative) {
        *actual_u4 = magnitude >= point0->actual_u4 ?
            0U :
            point0->actual_u4 - (uint32_t)magnitude;
    } else if (magnitude > (uint64_t)UINT32_MAX - point0->actual_u4) {
        *actual_u4 = UINT32_MAX;
    } else {
        *actual_u4 = point0->actual_u4 + (uint32_t)magnitude;
    }

    return true;
}
