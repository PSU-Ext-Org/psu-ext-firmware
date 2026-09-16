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

#include "../measure_svc_calibration_record.h"

#include "unity.h"

TEST_CASE("calibration record round trips valid table and rejects integrity errors", "[calibration][record]")
{
    const measure_svc_cal_table_t source = {
        .count = 6U,
        .points = {
            {.raw_code = 0, .actual_u4 = 0U, .pga_full_scale_mv = 256U},
            {.raw_code = 0, .actual_u4 = 0U, .pga_full_scale_mv = 512U},
            {.raw_code = 0, .actual_u4 = 0U, .pga_full_scale_mv = 2048U},
            {.raw_code = 600, .actual_u4 = 3000U, .pga_full_scale_mv = 256U},
            {.raw_code = 1234, .actual_u4 = 5678U, .pga_full_scale_mv = 512U},
            {.raw_code = 9999, .actual_u4 = 22222U, .pga_full_scale_mv = 2048U},
        },
        .cached_segment = 2U,
        .cache_valid = true,
    };
    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE];
    measure_svc_cal_table_t decoded = {0};

    TEST_ASSERT_TRUE(measure_svc_cal_record_encode(&source, record));
    TEST_ASSERT_TRUE(measure_svc_cal_record_decode(record, &decoded));
    TEST_ASSERT_EQUAL_UINT8(source.count, decoded.count);
    TEST_ASSERT_EQUAL_INT16(source.points[1].raw_code, decoded.points[1].raw_code);
    TEST_ASSERT_EQUAL_UINT32(source.points[4].actual_u4, decoded.points[4].actual_u4);
    TEST_ASSERT_EQUAL_UINT16(source.points[5].pga_full_scale_mv, decoded.points[5].pga_full_scale_mv);
    TEST_ASSERT_FALSE(decoded.cache_valid);
    TEST_ASSERT_EQUAL_UINT8(0U, decoded.cached_segment);

    record[4] = 0xFFU;
    TEST_ASSERT_FALSE(measure_svc_cal_record_decode(record, &decoded));

    TEST_ASSERT_TRUE(measure_svc_cal_record_encode(&source, record));
    record[MEASURE_SVC_CAL_RECORD_SIZE - 1U] ^= 0x55U;
    TEST_ASSERT_FALSE(measure_svc_cal_record_decode(record, &decoded));

    TEST_ASSERT_TRUE(measure_svc_cal_record_encode(&source, record));
    record[7] = 1U;
    TEST_ASSERT_FALSE(measure_svc_cal_record_decode(record, &decoded));
}
