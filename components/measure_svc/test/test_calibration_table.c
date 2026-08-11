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

#include "../measure_svc_calibration_table.h"

#include <limits.h>

#include "unity.h"

static measure_svc_cal_table_t make_table(uint8_t count)
{
    measure_svc_cal_table_t table = {.count = count};
    for (uint8_t i = 0; i < count; ++i) {
        table.points[i].raw_u4 = (uint32_t)i * 100U;
        table.points[i].actual_u4 = (uint32_t)i * 1000U;
    }
    return table;
}

TEST_CASE("calibration table validation rejects invalid shapes", "[calibration][table]")
{
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(NULL));

    measure_svc_cal_table_t table = make_table(0U);
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(1U);
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(2U);
    TEST_ASSERT_TRUE(measure_svc_cal_table_validate(&table));

    table = make_table(8U);
    TEST_ASSERT_TRUE(measure_svc_cal_table_validate(&table));

    table = make_table(9U);
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(3U);
    table.points[1].raw_u4 = table.points[0].raw_u4;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(3U);
    table.points[1].actual_u4 = table.points[0].actual_u4;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(3U);
    table.points[1].raw_u4 = 50U;
    table.points[2].raw_u4 = 40U;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_table(3U);
    table.points[1].actual_u4 = 500U;
    table.points[2].actual_u4 = 400U;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));
}

TEST_CASE("calibration table applies points interpolation extrapolation rounding and saturation", "[calibration][table]")
{
    measure_svc_cal_table_t table = {
        .count = 4U,
        .points = {
            {.raw_u4 = 100U, .actual_u4 = 1000U},
            {.raw_u4 = 200U, .actual_u4 = 2000U},
            {.raw_u4 = 400U, .actual_u4 = 5000U},
            {.raw_u4 = 800U, .actual_u4 = 9000U},
        },
    };
    uint32_t actual = 0U;

    for (uint8_t i = 0; i < table.count; ++i) {
        TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, table.points[i].raw_u4, &actual));
        TEST_ASSERT_EQUAL_UINT32(table.points[i].actual_u4, actual);
    }

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 150U, &actual));
    TEST_ASSERT_EQUAL_UINT32(1500U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 300U, &actual));
    TEST_ASSERT_EQUAL_UINT32(3500U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 600U, &actual));
    TEST_ASSERT_EQUAL_UINT32(7000U, actual);

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 50U, &actual));
    TEST_ASSERT_EQUAL_UINT32(500U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 900U, &actual));
    TEST_ASSERT_EQUAL_UINT32(10000U, actual);

    table = (measure_svc_cal_table_t){
        .count = 2U,
        .points = {
            {.raw_u4 = 10U, .actual_u4 = 10U},
            {.raw_u4 = 12U, .actual_u4 = 11U},
        },
    };
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 11U, &actual));
    TEST_ASSERT_EQUAL_UINT32(11U, actual);

    table = (measure_svc_cal_table_t){
        .count = 2U,
        .points = {
            {.raw_u4 = 10U, .actual_u4 = 1U},
            {.raw_u4 = 12U, .actual_u4 = 3U},
        },
    };
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 9U, &actual));
    TEST_ASSERT_EQUAL_UINT32(0U, actual);

    table = (measure_svc_cal_table_t){
        .count = 2U,
        .points = {
            {.raw_u4 = 0U, .actual_u4 = 0U},
            {.raw_u4 = 1U, .actual_u4 = UINT32_MAX},
        },
    };
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 2U, &actual));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, actual);
}

TEST_CASE("calibration table cache is only a lookup hint", "[calibration][table]")
{
    measure_svc_cal_table_t cached = {
        .count = 5U,
        .points = {
            {.raw_u4 = 0U, .actual_u4 = 0U},
            {.raw_u4 = 100U, .actual_u4 = 1000U},
            {.raw_u4 = 200U, .actual_u4 = 3000U},
            {.raw_u4 = 300U, .actual_u4 = 6000U},
            {.raw_u4 = 400U, .actual_u4 = 10000U},
        },
    };
    measure_svc_cal_table_t uncached = cached;
    uint32_t cached_value = 0U;
    uint32_t uncached_value = 0U;

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&cached, 50U, &cached_value));
    TEST_ASSERT_TRUE(cached.cache_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, cached.cached_segment);

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&cached, 250U, &cached_value));
    measure_svc_cal_table_invalidate_cache(&uncached);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&uncached, 250U, &uncached_value));
    TEST_ASSERT_EQUAL_UINT32(uncached_value, cached_value);
    TEST_ASSERT_EQUAL_UINT8(2U, cached.cached_segment);

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&cached, 450U, &cached_value));
    TEST_ASSERT_EQUAL_UINT8(3U, cached.cached_segment);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&cached, 0U, &cached_value));
    TEST_ASSERT_EQUAL_UINT8(0U, cached.cached_segment);
}
