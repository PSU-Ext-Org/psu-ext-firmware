/* Copyright 2026 PSU-EXT Authors
 * Licensed under the Apache License, Version 2.0. */
#include "measure_svc_calibration_record.h"

#include <string.h>
#include "esp_rom_crc.h"

enum { HEADER_SIZE = 8U, V3_POINT_SIZE = 8U, V4_POINT_SIZE = 12U };
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U); p[2] = (uint8_t)(v >> 16U); p[3] = (uint8_t)(v >> 24U); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8U); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }

bool measure_svc_cal_record_encode(const measure_svc_cal_table_t *table, uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE])
{
    if ((record == NULL) || !measure_svc_cal_table_validate(table)) return false;
    memset(record, 0, MEASURE_SVC_CAL_RECORD_SIZE);
    put32(record, 0x344C4143U); /* CAL4 */ put16(record + 4U, 4U); record[6] = table->count;
    for (uint8_t i = 0U; i < table->count; ++i) {
        uint8_t *p = record + HEADER_SIZE + (size_t)i * V4_POINT_SIZE;
        put32(p, (uint32_t)table->points[i].raw_code_q16);
        put16(p + 4U, table->points[i].pga_full_scale_mv);
        put32(p + 8U, table->points[i].actual_u4);
    }
    put32(record + MEASURE_SVC_CAL_RECORD_SIZE - 4U, esp_rom_crc32_le(0U, record, MEASURE_SVC_CAL_RECORD_SIZE - 4U));
    return true;
}

bool measure_svc_cal_record_decode_sized(const uint8_t *record, size_t size, measure_svc_cal_table_t *table)
{
    if ((record == NULL) || (table == NULL) || (size < HEADER_SIZE + 4U) || (record[7] != 0U) ||
        (get32(record + size - 4U) != esp_rom_crc32_le(0U, record, size - 4U))) return false;
    const bool v4 = (get32(record) == 0x344C4143U) && (get16(record + 4U) == 4U) && (size == MEASURE_SVC_CAL_RECORD_SIZE);
    const bool v3 = (get32(record) == 0x334C4143U) && (get16(record + 4U) == 3U) && (size == MEASURE_SVC_CAL_RECORD_V3_SIZE);
    if (!v3 && !v4) return false;
    measure_svc_cal_table_t candidate = {0}; candidate.count = record[6];
    if (candidate.count > MEASURE_SVC_CAL_MAX_POINTS) return false;
    const size_t point_size = v4 ? V4_POINT_SIZE : V3_POINT_SIZE;
    for (size_t i = HEADER_SIZE + (size_t)candidate.count * point_size; i < size - 4U; ++i) if (record[i] != 0U) return false;
    for (uint8_t i = 0U; i < candidate.count; ++i) {
        const uint8_t *p = record + HEADER_SIZE + (size_t)i * point_size;
        if (v4 && get16(p + 6U) != 0U) return false;
        candidate.points[i].raw_code_q16 = v4 ? (int32_t)get32(p) : (int32_t)(int16_t)get16(p) * 65536;
        candidate.points[i].pga_full_scale_mv = get16(p + (v4 ? 4U : 2U));
        candidate.points[i].actual_u4 = get32(p + (v4 ? 8U : 4U));
    }
    if (!measure_svc_cal_table_validate(&candidate)) return false;
    measure_svc_cal_table_invalidate_cache(&candidate); *table = candidate; return true;
}

bool measure_svc_cal_record_decode(const uint8_t record[MEASURE_SVC_CAL_RECORD_SIZE], measure_svc_cal_table_t *table)
{ return measure_svc_cal_record_decode_sized(record, MEASURE_SVC_CAL_RECORD_SIZE, table); }
