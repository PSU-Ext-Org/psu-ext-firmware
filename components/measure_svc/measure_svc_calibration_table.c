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

bool measure_svc_cal_table_validate(const measure_svc_cal_table_t *table)
{
    if ((table == NULL) ||
        (table->count < 2U) ||
        (table->count > MEASURE_SVC_CAL_MAX_POINTS)) {
        return false;
    }

    for (uint8_t index = 1U; index < table->count; ++index) {
        const measure_svc_cal_table_point_t *previous = &table->points[index - 1U];
        const measure_svc_cal_table_point_t *current = &table->points[index];

        if ((current->raw_u4 <= previous->raw_u4) ||
            (current->actual_u4 <= previous->actual_u4)) {
            return false;
        }
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

static uint8_t measure_svc_cal_table_find_segment(
    measure_svc_cal_table_t *table,
    uint32_t raw_u4)
{
    const uint8_t last = table->count - 1U;

    if (raw_u4 <= table->points[0].raw_u4) {
        table->cached_segment = 0U;
        table->cache_valid = true;
        return 0U;
    }

    if (raw_u4 >= table->points[last].raw_u4) {
        table->cached_segment = last - 1U;
        table->cache_valid = true;
        return last - 1U;
    }

    if (table->cache_valid) {
        const uint8_t cached = table->cached_segment;

        if ((cached < last) &&
            (table->points[cached].raw_u4 <= raw_u4) &&
            (raw_u4 <= table->points[cached + 1U].raw_u4)) {
            return cached;
        }
    }

    uint8_t low = 0U;
    uint8_t high = last;

    while ((uint8_t)(high - low) > 1U) {
        const uint8_t middle = low + ((high - low) / 2U);

        if (raw_u4 < table->points[middle].raw_u4) {
            high = middle;
        } else {
            low = middle;
        }
    }

    table->cached_segment = low;
    table->cache_valid = true;
    return low;
}

/*
 * Calculate round(multiplicand * multiplier / positive_divisor) without
 * overflowing a 64-bit intermediate. All inputs originate from uint32_t
 * calibration values, so the decomposed products and final magnitude fit in
 * uint64_t even for extreme endpoint extrapolation.
 */
static uint64_t measure_svc_cal_table_round_mul_div_magnitude(
    uint64_t multiplicand,
    uint64_t multiplier,
    uint64_t positive_divisor)
{
    const uint64_t whole = multiplicand / positive_divisor;
    const uint64_t remainder = multiplicand % positive_divisor;
    const uint64_t remainder_product = remainder * multiplier;
    uint64_t result = whole * multiplier;

    result += remainder_product / positive_divisor;

    const uint64_t fractional_remainder = remainder_product % positive_divisor;
    const uint64_t half_threshold = (positive_divisor / 2U) + (positive_divisor % 2U);
    if (fractional_remainder >= half_threshold) {
        ++result;
    }

    return result;
}

static int64_t measure_svc_cal_table_round_div_signed(
    int64_t numerator,
    int64_t positive_denominator)
{
    int64_t quotient = numerator / positive_denominator;
    const int64_t remainder = numerator % positive_denominator;
    const int64_t half_threshold =
        (positive_denominator / 2) + (positive_denominator % 2);

    if (remainder >= half_threshold) {
        ++quotient;
    } else if (remainder <= -half_threshold) {
        --quotient;
    }

    return quotient;
}

bool measure_svc_cal_table_apply_u4(
    measure_svc_cal_table_t *table,
    uint32_t raw_u4,
    uint32_t *actual_u4)
{
    if ((actual_u4 == NULL) || !measure_svc_cal_table_validate(table)) {
        return false;
    }

    const uint8_t segment = measure_svc_cal_table_find_segment(table, raw_u4);
    const measure_svc_cal_table_point_t *point0 = &table->points[segment];
    const measure_svc_cal_table_point_t *point1 = &table->points[segment + 1U];

    if (raw_u4 == point0->raw_u4) {
        *actual_u4 = point0->actual_u4;
        return true;
    }
    if (raw_u4 == point1->raw_u4) {
        *actual_u4 = point1->actual_u4;
        return true;
    }

    const int64_t raw_span = (int64_t)point1->raw_u4 - (int64_t)point0->raw_u4;
    const int64_t actual_span = (int64_t)point1->actual_u4 - (int64_t)point0->actual_u4;
    const int64_t raw_delta = (int64_t)raw_u4 - (int64_t)point0->raw_u4;
    const uint64_t raw_delta_magnitude = raw_delta < 0 ?
        (uint64_t)(-raw_delta) :
        (uint64_t)raw_delta;

    if (raw_delta_magnitude <= (uint64_t)INT64_MAX / (uint64_t)actual_span) {
        const int64_t numerator = raw_delta * actual_span;
        const int64_t correction = measure_svc_cal_table_round_div_signed(
            numerator,
            raw_span);

        if (correction < 0) {
            const uint64_t magnitude = (uint64_t)(-correction);
            *actual_u4 = magnitude >= point0->actual_u4 ?
                0U :
                point0->actual_u4 - (uint32_t)magnitude;
        } else if ((uint64_t)correction > (uint64_t)UINT32_MAX - point0->actual_u4) {
            *actual_u4 = UINT32_MAX;
        } else {
            *actual_u4 = point0->actual_u4 + (uint32_t)correction;
        }

        return true;
    }

    /* Extreme uint32_t ranges can overflow raw_delta * actual_span in int64_t. */
    const uint64_t correction_magnitude = measure_svc_cal_table_round_mul_div_magnitude(
        raw_delta_magnitude,
        (uint64_t)actual_span,
        (uint64_t)raw_span);

    if (raw_delta < 0) {
        *actual_u4 = correction_magnitude >= point0->actual_u4 ?
            0U :
            point0->actual_u4 - (uint32_t)correction_magnitude;
    } else if (correction_magnitude > (uint64_t)UINT32_MAX - point0->actual_u4) {
        *actual_u4 = UINT32_MAX;
    } else {
        *actual_u4 = point0->actual_u4 + (uint32_t)correction_magnitude;
    }

    return true;
}
