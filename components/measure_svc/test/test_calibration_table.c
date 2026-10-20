/* Copyright 2026 PSU-EXT Authors */

#include "../measure_svc_calibration_table.h"

#include <limits.h>

#include "unity.h"
#include "sdkconfig.h"

static measure_svc_cal_table_t make_valid_table(void)
{
    return (measure_svc_cal_table_t){
        .count = 6U,
        .points = {
            {.raw_code_q16 = (50) * 65536, .actual_u4 = 500U, .pga_full_scale_mv = 256U},
            {.raw_code_q16 = (100) * 65536, .actual_u4 = 1000U, .pga_full_scale_mv = 512U},
            {.raw_code_q16 = (10) * 65536, .actual_u4 = 100U, .pga_full_scale_mv = 2048U},
            {.raw_code_q16 = (150) * 65536, .actual_u4 = 1500U, .pga_full_scale_mv = 256U},
            {.raw_code_q16 = (200) * 65536, .actual_u4 = 2000U, .pga_full_scale_mv = 512U},
            {.raw_code_q16 = (20) * 65536, .actual_u4 = 400U, .pga_full_scale_mv = 2048U},
        },
    };
}

TEST_CASE("calibration validates every configured PGA independently", "[calibration][table]")
{
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(NULL));
    measure_svc_cal_table_t table = make_valid_table();
    TEST_ASSERT_TRUE(measure_svc_cal_table_validate(&table));

    table.points[5].pga_full_scale_mv = 512U;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_valid_table();
    table.count = 2U; /* One point in each of two ranges. */
#if CONFIG_PSUEXT_CAL_REQUIRE_MIN_POINTS_PER_PGA
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));
#else
    TEST_ASSERT_TRUE(measure_svc_cal_table_validate(&table));
#endif

    table = make_valid_table();
    table.points[3].raw_code_q16 = table.points[0].raw_code_q16;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_valid_table();
    table.points[5].actual_u4 = table.points[2].actual_u4;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_valid_table();
    table.points[0].pga_full_scale_mv = 1024U;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));

    table = make_valid_table();
    table.count = MEASURE_SVC_CAL_MAX_POINTS + 1U;
    TEST_ASSERT_FALSE(measure_svc_cal_table_validate(&table));
}

TEST_CASE("calibration interpolates only within the sample PGA", "[calibration][table]")
{
    measure_svc_cal_table_t table = make_valid_table();
    uint32_t actual = 0U;

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 100U, 256U, &actual));
    TEST_ASSERT_EQUAL_UINT32(1000U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 150U, 512U, &actual));
    TEST_ASSERT_EQUAL_UINT32(1500U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 15U, 2048U, &actual));
    TEST_ASSERT_EQUAL_UINT32(250U, actual);
    TEST_ASSERT_FALSE(measure_svc_cal_table_apply_u4(&table, 15U, 1024U, &actual));

    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 50U, 512U, &actual));
    TEST_ASSERT_EQUAL_UINT32(500U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 30U, 2048U, &actual));
    TEST_ASSERT_EQUAL_UINT32(700U, actual);

    table = (measure_svc_cal_table_t){
        .count = 6U,
        .points = {
            {.raw_code_q16 = (0) * 65536, .actual_u4 = 0U, .pga_full_scale_mv = 256U},
            {.raw_code_q16 = (0) * 65536, .actual_u4 = 0U, .pga_full_scale_mv = 512U},
            {.raw_code_q16 = (0) * 65536, .actual_u4 = 0U, .pga_full_scale_mv = 2048U},
            {.raw_code_q16 = (INT16_MAX) * 65536, .actual_u4 = 44800U, .pga_full_scale_mv = 256U},
            {.raw_code_q16 = (INT16_MAX) * 65536, .actual_u4 = 89600U, .pga_full_scale_mv = 512U},
            {.raw_code_q16 = (INT16_MAX) * 65536, .actual_u4 = 358400U, .pga_full_scale_mv = 2048U},
        },
    };
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 1, 512U, &actual));
    TEST_ASSERT_EQUAL_UINT32(3U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 4, 512U, &actual));
    TEST_ASSERT_EQUAL_UINT32(11U, actual);
    TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, 1, 2048U, &actual));
    TEST_ASSERT_EQUAL_UINT32(11U, actual);
}

TEST_CASE("calibration supports 32 interleaved points", "[calibration][table]")
{
    measure_svc_cal_table_t table = {.count = MEASURE_SVC_CAL_MAX_POINTS};
    for (uint8_t index = 0U; index < MEASURE_SVC_CAL_MAX_POINTS; ++index) {
        const uint8_t ordinal = (uint8_t)(index / 3U + 1U);
        static const uint16_t pgas[] = {256U, 512U, 2048U};
        table.points[index] = (measure_svc_cal_table_point_t){
            .raw_code_q16 = (int32_t)ordinal * 100 * 65536,
            .actual_u4 = (uint32_t)ordinal * 1000U,
            .pga_full_scale_mv = pgas[index % 3U],
        };
    }
    TEST_ASSERT_TRUE(measure_svc_cal_table_validate(&table));
}
