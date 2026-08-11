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
 * @file measure_svc_calibration_compat.c
 * @brief Legacy two-point calibration API adapters.
 */

#include "measure_svc.h"
#include "measure_svc_internal.h"

static void get_first_two_points(
    measure_kind_t kind,
    measure_channel_t channel,
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2)
{
    if (point1 != NULL) {
        (void)measure_svc_calibration_get_point(kind, channel, 1U, point1);
    }
    if (point2 != NULL) {
        (void)measure_svc_calibration_get_point(kind, channel, 2U, point2);
    }
}

esp_err_t measure_svc_calibrate_input_voltage_point(
    uint8_t point_index,
    uint32_t actual_voltage_u4,
    measure_svc_cal_point_t *stored_point)
{
    if ((point_index != 1U) && (point_index != 2U)) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_calibration_capture_point(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_0,
        point_index,
        actual_voltage_u4,
        stored_point);
}

esp_err_t measure_svc_calibrate_ch0_point(
    uint8_t point_index,
    uint32_t actual_voltage_u4,
    measure_svc_cal_point_t *stored_point)
{
    if ((point_index != 1U) && (point_index != 2U)) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_calibration_capture_point(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_1,
        point_index,
        actual_voltage_u4,
        stored_point);
}

esp_err_t measure_svc_calibrate_current_point(
    uint8_t point_index,
    uint32_t actual_current_u4,
    measure_svc_cal_point_t *stored_point)
{
    if ((point_index != 1U) && (point_index != 2U)) {
        return ESP_ERR_INVALID_ARG;
    }
    return measure_svc_calibration_capture_point(
        MEASURE_KIND_CURRENT,
        MEASURE_CHANNEL_1,
        point_index,
        actual_current_u4,
        stored_point);
}

esp_err_t measure_svc_get_input_voltage_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point)
{
    return measure_svc_calibration_get_point(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_0,
        point_index,
        point);
}

esp_err_t measure_svc_get_ch0_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point)
{
    return measure_svc_calibration_get_point(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_1,
        point_index,
        point);
}

esp_err_t measure_svc_get_current_cal_point(
    uint8_t point_index,
    measure_svc_cal_point_t *point)
{
    return measure_svc_calibration_get_point(
        MEASURE_KIND_CURRENT,
        MEASURE_CHANNEL_1,
        point_index,
        point);
}

uint32_t measure_svc_apply_input_voltage_calibration_u4(uint32_t adc_voltage_u4)
{
    return measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_0,
        adc_voltage_u4);
}

uint32_t measure_svc_apply_ch0_calibration_u4(uint32_t adc_voltage_u4)
{
    return measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_VOLTAGE,
        MEASURE_CHANNEL_1,
        adc_voltage_u4);
}

uint32_t measure_svc_apply_current_calibration_u4(uint32_t adc_voltage_u4)
{
    return measure_svc_calibration_apply_target_u4(
        MEASURE_KIND_CURRENT,
        MEASURE_CHANNEL_1,
        adc_voltage_u4);
}

void measure_svc_get_input_voltage_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2)
{
    get_first_two_points(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_0, point1, point2);
}

void measure_svc_get_ch0_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2)
{
    get_first_two_points(MEASURE_KIND_VOLTAGE, MEASURE_CHANNEL_1, point1, point2);
}

void measure_svc_get_current_cal_points(
    measure_svc_cal_point_t *point1,
    measure_svc_cal_point_t *point2)
{
    get_first_two_points(MEASURE_KIND_CURRENT, MEASURE_CHANNEL_1, point1, point2);
}
