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
 * @file measure_svc_calibration_record.c
 * @brief Stable byte encoding for persisted calibration tables.
 */

#include "measure_svc_calibration_record.h"

#include <stddef.h>
#include <string.h>

#include "esp_rom_crc.h"

enum {
    CAL_RECORD_MAGIC = 0x314C4143U, /* Little-endian bytes "CAL1". */
    CAL_RECORD_VERSION = 1U,
    CAL_RECORD_MAGIC_OFFSET = 0U,
    CAL_RECORD_VERSION_OFFSET = 4U,
    CAL_RECORD_COUNT_OFFSET = 6U,
    CAL_RECORD_RESERVED_OFFSET = 7U,
    CAL_RECORD_POINTS_OFFSET = 8U,
    CAL_RECORD_POINT_SIZE = 8U,
    CAL_RECORD_CRC_OFFSET =
        CAL_RECORD_POINTS_OFFSET +
        (MEASURE_SVC_CAL_MAX_POINTS * CAL_RECORD_POINT_SIZE),
};

static void write_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint16_t read_u16_le(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) |
        ((uint32_t)source[3] << 24U);
}

static uint32_t record_crc(const uint8_t *record)
{
    return esp_rom_crc32_le(0U, record, CAL_RECORD_CRC_OFFSET);
}

bool measure_svc_cal_record_encode(
    const measure_svc_cal_table_t *table,
    uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE])
{
    if ((record == NULL) || !measure_svc_cal_table_validate(table)) {
        return false;
    }

    memset(record, 0, MEASURE_SVC_CAL_RECORD_SIZE);
    write_u32_le(&record[CAL_RECORD_MAGIC_OFFSET], CAL_RECORD_MAGIC);
    write_u16_le(&record[CAL_RECORD_VERSION_OFFSET], CAL_RECORD_VERSION);
    record[CAL_RECORD_COUNT_OFFSET] = table->count;

    for (uint8_t index = 0U; index < table->count; ++index) {
        const size_t offset =
            CAL_RECORD_POINTS_OFFSET + ((size_t)index * CAL_RECORD_POINT_SIZE);
        write_u32_le(&record[offset], table->points[index].raw_u4);
        write_u32_le(&record[offset + 4U], table->points[index].actual_u4);
    }

    write_u32_le(&record[CAL_RECORD_CRC_OFFSET], record_crc(record));
    return true;
}

bool measure_svc_cal_record_decode(
    const uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE],
    measure_svc_cal_table_t *table)
{
    if ((record == NULL) || (table == NULL) ||
        (read_u32_le(&record[CAL_RECORD_MAGIC_OFFSET]) != CAL_RECORD_MAGIC) ||
        (read_u16_le(&record[CAL_RECORD_VERSION_OFFSET]) != CAL_RECORD_VERSION) ||
        (record[CAL_RECORD_RESERVED_OFFSET] != 0U) ||
        (read_u32_le(&record[CAL_RECORD_CRC_OFFSET]) != record_crc(record))) {
        return false;
    }

    measure_svc_cal_table_t candidate = {0};
    candidate.count = record[CAL_RECORD_COUNT_OFFSET];
    if (candidate.count > MEASURE_SVC_CAL_MAX_POINTS) {
        return false;
    }

    const size_t unused_offset =
        CAL_RECORD_POINTS_OFFSET + ((size_t)candidate.count * CAL_RECORD_POINT_SIZE);
    for (size_t offset = unused_offset; offset < CAL_RECORD_CRC_OFFSET; ++offset) {
        if (record[offset] != 0U) {
            return false;
        }
    }

    for (uint8_t index = 0U; index < candidate.count; ++index) {
        const size_t offset =
            CAL_RECORD_POINTS_OFFSET + ((size_t)index * CAL_RECORD_POINT_SIZE);
        candidate.points[index].raw_u4 = read_u32_le(&record[offset]);
        candidate.points[index].actual_u4 = read_u32_le(&record[offset + 4U]);
    }

    if (!measure_svc_cal_table_validate(&candidate)) {
        return false;
    }

    measure_svc_cal_table_invalidate_cache(&candidate);
    *table = candidate;
    return true;
}
