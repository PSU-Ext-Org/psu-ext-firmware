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

#include <math.h>
#include "unity.h"
#include "measure_svc_precision.h"
#include "measure_svc_samples.h"
#include "../measure_svc_calibration_table.h"
#include "../measure_svc_event_bus.h"
#include "../measure_svc_history.h"
#include "../measure_svc_power.h"

static void publish(measure_channel_t channel, measure_kind_t kind, double value)
{
    const measure_svc_sample_event_t event = {
        .channel = channel,
        .kind = kind,
        .physical_input = MEASURE_INPUT_UNUSED,
        .precise_u4 = value,
        .value_u4 = measure_svc_precision_to_u4(value),
    };
    measure_svc_event_bus_publish(&event);
}

static void init_history(void)
{
    TEST_ESP_OK(measure_svc_history_init());
    TEST_ESP_OK(measure_svc_history_register_listeners());
}

static uint32_t average(measure_channel_t channel, measure_kind_t kind, size_t count)
{
    uint32_t value;
    TEST_ESP_OK(measure_svc_history_average_latest(channel, kind, count, &value));
    return value;
}

TEST_CASE("precision converts only at unsigned boundaries", "[precision]")
{
    TEST_ASSERT_EQUAL_UINT32(0U, measure_svc_precision_to_u4(-2.0));
    TEST_ASSERT_EQUAL_UINT32(0U, measure_svc_precision_to_u4(0.0));
    TEST_ASSERT_EQUAL_UINT32(0U, measure_svc_precision_to_u4(0.499));
    TEST_ASSERT_EQUAL_UINT32(1U, measure_svc_precision_to_u4(0.5));
    TEST_ASSERT_EQUAL_UINT32(2U, measure_svc_precision_to_u4(1.5));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, measure_svc_precision_to_u4((double)UINT32_MAX - 0.25));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, measure_svc_precision_to_u4((double)UINT32_MAX + 100.0));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, measure_svc_precision_to_u4(INFINITY));
    TEST_ASSERT_EQUAL_UINT32(0U, measure_svc_precision_to_u4(-INFINITY));
    TEST_ASSERT_EQUAL_UINT32(0U, measure_svc_precision_to_u4(NAN));
}

TEST_CASE("calibration retains signed fractional interpolation and extrapolation", "[calibration][precision]")
{
    measure_svc_cal_table_t table = {.count = 6U};
    const uint16_t pgas[] = {256U, 512U, 2048U};
    for (size_t i = 0U; i < 3U; ++i) {
        table.points[2U * i] = (measure_svc_cal_table_point_t){
            .raw_code_q16 = 32768, .actual_u4 = 0U, .pga_full_scale_mv = pgas[i]};
        table.points[2U * i + 1U] = (measure_svc_cal_table_point_t){
            .raw_code_q16 = 3 * 65536, .actual_u4 = 2U, .pga_full_scale_mv = pgas[i]};
    }
    const int16_t codes[] = {-2, 0, 1, 2, 3, 4};
    const double expected[] = {-2.0, -0.4, 0.4, 1.2, 2.0, 2.8};
    for (size_t range = 0U; range < 3U; ++range) {
        for (size_t i = 0U; i < sizeof(codes) / sizeof(codes[0]); ++i) {
            double precise;
            uint32_t projected;
            TEST_ASSERT_TRUE(measure_svc_cal_table_apply_precise_u4(&table, codes[i], pgas[range], &precise));
            TEST_ASSERT_TRUE(fabs(precise - expected[i]) < 1e-10);
            TEST_ASSERT_TRUE(measure_svc_cal_table_apply_u4(&table, codes[i], pgas[range], &projected));
            TEST_ASSERT_EQUAL_UINT32(measure_svc_precision_to_u4(expected[i]), projected);
        }
    }
    double precise;
    TEST_ASSERT_FALSE(measure_svc_cal_table_apply_precise_u4(&table, 0, 1024U, &precise));
    TEST_ASSERT_FALSE(measure_svc_cal_table_apply_precise_u4(&table, 0, 256U, NULL));
}

TEST_CASE("history averages signed fractions before projecting public samples", "[precision]")
{
    init_history();
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, -2.0);
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 2.0);
    TEST_ASSERT_EQUAL_UINT32(0U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 2U));
    TEST_ASSERT_EQUAL_UINT32(2U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 1U));
    measure_svc_sample_t exported[2];
    size_t copied;
    TEST_ESP_OK(measure_svc_history_copy_latest_samples(
        MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 2U, exported, &copied));
    TEST_ASSERT_EQUAL_UINT32(2U, copied);
    TEST_ASSERT_EQUAL_UINT32(0U, exported[0].value_u4);
    TEST_ASSERT_EQUAL_UINT32(2U, exported[1].value_u4);
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, -0.4);
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, -1.4);
    TEST_ASSERT_EQUAL_UINT32(0U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 2U));
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 0.4);
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 0.4);
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 1.4);
    TEST_ASSERT_EQUAL_UINT32(1U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 3U));
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 99.0);
    TEST_ASSERT_EQUAL_UINT32(99U, average(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 3U));
}

TEST_CASE("history averages newest maximum window after ring wrap", "[precision]")
{
    init_history();
    for (size_t i = 0U; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY + 17U; ++i) {
        publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, (i & 1U) ? -2.0 : 2.0);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, MEASURE_SVC_MAX_AVERAGE_COUNT));
    publish(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 20.0);
    TEST_ASSERT_EQUAL_UINT32(11U, average(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, 2U));
}

TEST_CASE("power retains negative products and averages products rather than inputs", "[precision]")
{
    init_history();
    TEST_ESP_OK(measure_svc_power_init());
    /* Establish a complete pair irrespective of earlier tests' pending inputs. */
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, -2.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 2.0);
    TEST_ASSERT_EQUAL_UINT32(0U, average(MEASURE_CHANNEL_1, MEASURE_KIND_POWER, 2U));
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 2.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 30000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 4.0);
    TEST_ASSERT_EQUAL_UINT32(7U, average(MEASURE_CHANNEL_1, MEASURE_KIND_POWER, 2U));
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0.4);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0);
    publish(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 1.4);
    TEST_ASSERT_EQUAL_UINT32(1U, average(MEASURE_CHANNEL_1, MEASURE_KIND_POWER, 2U));
}
