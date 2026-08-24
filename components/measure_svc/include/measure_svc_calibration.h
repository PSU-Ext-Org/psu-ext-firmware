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

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "measure_types.h"

/**
 * @file measure_svc_calibration.h
 * @brief Transactional multi-point calibration public API.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_SVC_CAL_MAX_POINTS 8U /**< Maximum points in one target table. */

/** @brief One raw-to-reference calibration point in u4 fixed point. */
typedef struct {
    uint32_t raw_voltage_u4; /**< Uncalibrated ADC-domain value times 10,000. */
    uint32_t actual_voltage_u4; /**< Reference physical value times 10,000. */
} measure_svc_cal_point_t;

/** @brief State of the single global calibration transaction. */
typedef enum {
    MEASURE_SVC_CAL_TRANSACTION_IDLE = 0, /**< No target is being edited. */
    MEASURE_SVC_CAL_TRANSACTION_OPEN, /**< Staging table can be edited. */
    MEASURE_SVC_CAL_TRANSACTION_COMMITTING, /**< NVS commit is in progress. */
} measure_svc_cal_transaction_state_t;

/** @brief Snapshot of transaction state and its selected target. */
typedef struct {
    measure_svc_cal_transaction_state_t state; /**< Current lifecycle state. */
    measure_kind_t kind; /**< Selected quantity when not idle. */
    measure_channel_t channel; /**< Selected channel when not idle. */
} measure_svc_cal_transaction_t;

/** @brief Open a transaction by copying the target's active table to staging. */
esp_err_t measure_svc_calibration_start(measure_kind_t kind, measure_channel_t channel);
/** @brief Read the current transaction state and selected target. */
esp_err_t measure_svc_calibration_get_transaction(measure_svc_cal_transaction_t *transaction);
/** @brief Discard staging changes and return an open transaction to idle. */
esp_err_t measure_svc_calibration_abort(void);
/**
 * @brief Capture a stable fresh raw window and stage one reference point.
 * @param kind Voltage or current calibration target.
 * @param channel Logical target channel.
 * @param point_index One-based point index from 1 through 8.
 * @param actual_u4 Reference physical value multiplied by 10,000.
 * @param stored_point Optional output receiving the staged raw/reference pair.
 */
esp_err_t measure_svc_calibration_capture_point(
    measure_kind_t kind, measure_channel_t channel, uint8_t point_index,
    uint32_t actual_u4, measure_svc_cal_point_t *stored_point);
/** @brief Read one staged point for the selected target, otherwise an active point. */
esp_err_t measure_svc_calibration_get_point(
    measure_kind_t kind, measure_channel_t channel, uint8_t point_index,
    measure_svc_cal_point_t *point);
/** @brief Clear the selected target's staging table within an open transaction. */
esp_err_t measure_svc_calibration_clear(measure_kind_t kind, measure_channel_t channel);
/** @brief Read the staged or active point count for a target. */
esp_err_t measure_svc_calibration_get_count(
    measure_kind_t kind, measure_channel_t channel, uint8_t *count);
/** @brief Validate, persist, and atomically publish the staged target table. */
esp_err_t measure_svc_calibration_commit(void);

#ifdef __cplusplus
}
#endif
