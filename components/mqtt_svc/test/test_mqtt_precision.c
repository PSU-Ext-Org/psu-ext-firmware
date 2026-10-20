/* Copyright 2026 PSU-EXT Authors. Licensed under Apache-2.0. */
#include "../mqtt_svc_window.h"
#include "unity.h"

TEST_CASE("mqtt averages signed fractions and projects extrema at summary", "[mqtt][precision]")
{
    mqtt_svc_window_t window;
    mqtt_svc_telemetry_t telemetry;
    mqtt_svc_window_reset(&window);
    mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, -2.0);
    mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, 2.0);
    mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, 0.4);
    mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, 0.4);
    mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, 1.4);
    mqtt_svc_window_add(&window, MEASURE_KIND_POWER, -2.0);
    mqtt_svc_window_add(&window, MEASURE_KIND_POWER, 2.0);
    TEST_ASSERT_TRUE(mqtt_svc_window_summarize(&window, &telemetry));
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.voltage_u4);
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.voltage_min_u4);
    TEST_ASSERT_EQUAL_UINT32(2U, telemetry.voltage_max_u4);
    TEST_ASSERT_EQUAL_UINT32(1U, telemetry.current_u4);
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.current_min_u4);
    TEST_ASSERT_EQUAL_UINT32(1U, telemetry.current_max_u4);
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.power_u4);
    mqtt_svc_window_reset(&window);
    mqtt_svc_window_add(&window, MEASURE_KIND_VOLTAGE, -1.0);
    mqtt_svc_window_add(&window, MEASURE_KIND_CURRENT, -0.4);
    TEST_ASSERT_TRUE(mqtt_svc_window_summarize(&window, &telemetry));
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.current_u4);
    TEST_ASSERT_EQUAL_UINT32(0U, telemetry.current_max_u4);
}
