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
 * @file measure_svc_calibration_record.h
 * @brief Stable byte encoding for persisted calibration tables.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "measure_svc_calibration_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_SVC_CAL_RECORD_SIZE 76U

bool measure_svc_cal_record_encode(
    const measure_svc_cal_table_t *table,
    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE]);

bool measure_svc_cal_record_decode(
    const uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE],
    measure_svc_cal_table_t *table);

#ifdef __cplusplus
}
#endif
