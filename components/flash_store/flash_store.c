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
 * @file flash_store.c
 * @brief External SPI flash storage helper.
 */

#include "flash_store.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_flash_spi_init.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

static const char *TAG = "flash_store";

static const spi_host_device_t FLASH_STORE_SPI_HOST = SPI2_HOST;
static const gpio_num_t FLASH_STORE_SPI_MOSI_GPIO = GPIO_NUM_11;
static const gpio_num_t FLASH_STORE_SPI_SCLK_GPIO = GPIO_NUM_12;
static const gpio_num_t FLASH_STORE_SPI_MISO_GPIO = GPIO_NUM_13;
static const gpio_num_t FLASH_STORE_SPI_CS_GPIO = GPIO_NUM_14;
static const int FLASH_STORE_SPI_FREQ_MHZ = 20;

enum {
    FLASH_STORE_SECTOR_SIZE_BYTES = 4096U,
    FLASH_STORE_EXPECTED_PHYSICAL_SIZE_BYTES = 512U * 1024U,
    FLASH_STORE_METADATA_SIZE_BYTES = 16U * 1024U,
    FLASH_STORE_VOLTAGE_SIZE_BYTES = 216U * 1024U,
    FLASH_STORE_CURRENT_SIZE_BYTES = 216U * 1024U,
    FLASH_STORE_KEY_VALUE_SIZE_BYTES = 64U * 1024U,

    FLASH_STORE_METADATA_OFFSET_BYTES = 0U,
    FLASH_STORE_VOLTAGE_OFFSET_BYTES =
        FLASH_STORE_METADATA_OFFSET_BYTES + FLASH_STORE_METADATA_SIZE_BYTES,
    FLASH_STORE_CURRENT_OFFSET_BYTES =
        FLASH_STORE_VOLTAGE_OFFSET_BYTES + FLASH_STORE_VOLTAGE_SIZE_BYTES,
    FLASH_STORE_KEY_VALUE_OFFSET_BYTES =
        FLASH_STORE_CURRENT_OFFSET_BYTES + FLASH_STORE_CURRENT_SIZE_BYTES,
    FLASH_STORE_TOTAL_RESERVED_BYTES =
        FLASH_STORE_METADATA_SIZE_BYTES +
        FLASH_STORE_VOLTAGE_SIZE_BYTES +
        FLASH_STORE_CURRENT_SIZE_BYTES +
        FLASH_STORE_KEY_VALUE_SIZE_BYTES,

    FLASH_STORE_RECORD_MAGIC = 0x5058454DU, /* "PXEM" */
    FLASH_STORE_RECORD_VERSION = 1U,
};

typedef struct {
    uint32_t magic;
    uint64_t unix_time_ms;
    uint32_t value_u6;
    uint32_t sequence;
    uint32_t crc32;
    uint32_t reserved;
} flash_store_measurement_record_t;

typedef struct {
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t count;
    bool writable;
} flash_store_measurement_region_t;

static_assert(sizeof(flash_store_measurement_record_t) == 32U, "measurement record must stay 32 bytes");

static esp_flash_t *s_flash_chip;
static const esp_partition_t *s_flash_partition;
static bool s_flash_initialized;
static flash_store_measurement_region_t s_voltage_region = {
    .name = "voltage",
    .offset = FLASH_STORE_VOLTAGE_OFFSET_BYTES,
    .size = FLASH_STORE_VOLTAGE_SIZE_BYTES,
    .writable = true,
};
static flash_store_measurement_region_t s_current_region = {
    .name = "current",
    .offset = FLASH_STORE_CURRENT_OFFSET_BYTES,
    .size = FLASH_STORE_CURRENT_SIZE_BYTES,
    .writable = true,
};

static bool flash_store_is_aligned(uint32_t value)
{
    return (value % FLASH_STORE_SECTOR_SIZE_BYTES) == 0U;
}

static bool flash_store_is_empty_record(const flash_store_measurement_record_t *record)
{
    const uint8_t *raw = (const uint8_t *)record;

    for (size_t i = 0; i < sizeof(*record); ++i) {
        if (raw[i] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static uint32_t flash_store_record_crc(const flash_store_measurement_record_t *record)
{
    flash_store_measurement_record_t crc_record = *record;
    crc_record.crc32 = 0;
    return esp_rom_crc32_le(0, (const uint8_t *)&crc_record, sizeof(crc_record));
}

static bool flash_store_record_is_valid(const flash_store_measurement_record_t *record)
{
    return (record->magic == FLASH_STORE_RECORD_MAGIC) &&
           (record->reserved == FLASH_STORE_RECORD_VERSION) &&
           (record->value_u6 <= FLASH_STORE_MEASUREMENT_VALUE_U6_MAX) &&
           (record->unix_time_ms != 0U) &&
           (record->crc32 == flash_store_record_crc(record));
}

static esp_err_t flash_store_validate_layout(uint32_t physical_size)
{
    if (!flash_store_is_aligned(physical_size) ||
        !flash_store_is_aligned(FLASH_STORE_METADATA_SIZE_BYTES) ||
        !flash_store_is_aligned(FLASH_STORE_VOLTAGE_SIZE_BYTES) ||
        !flash_store_is_aligned(FLASH_STORE_CURRENT_SIZE_BYTES) ||
        !flash_store_is_aligned(FLASH_STORE_KEY_VALUE_SIZE_BYTES)) {
        ESP_LOGE(TAG, "Flash layout sizes must be %u-byte aligned", FLASH_STORE_SECTOR_SIZE_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    if (physical_size != FLASH_STORE_EXPECTED_PHYSICAL_SIZE_BYTES) {
        ESP_LOGW(
            TAG,
            "Flash layout is fixed for %u bytes but detected flash has %" PRIu32 " bytes",
            FLASH_STORE_EXPECTED_PHYSICAL_SIZE_BYTES,
            physical_size);
    }

    if (FLASH_STORE_TOTAL_RESERVED_BYTES > physical_size) {
        ESP_LOGE(
            TAG,
            "Flash layout requires %u bytes but detected flash has %" PRIu32 " bytes",
            FLASH_STORE_TOTAL_RESERVED_BYTES,
            physical_size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "layout metadata offset=0x%06X size=%u", FLASH_STORE_METADATA_OFFSET_BYTES, FLASH_STORE_METADATA_SIZE_BYTES);
    ESP_LOGI(TAG, "layout voltage  offset=0x%06X size=%u", FLASH_STORE_VOLTAGE_OFFSET_BYTES, FLASH_STORE_VOLTAGE_SIZE_BYTES);
    ESP_LOGI(TAG, "layout current  offset=0x%06X size=%u", FLASH_STORE_CURRENT_OFFSET_BYTES, FLASH_STORE_CURRENT_SIZE_BYTES);
    ESP_LOGI(TAG, "layout kv       offset=0x%06X size=%u", FLASH_STORE_KEY_VALUE_OFFSET_BYTES, FLASH_STORE_KEY_VALUE_SIZE_BYTES);
    ESP_LOGI(TAG, "layout reserved size=%u", FLASH_STORE_TOTAL_RESERVED_BYTES);

    return ESP_OK;
}

static esp_err_t flash_store_scan_region(flash_store_measurement_region_t *region)
{
    if ((s_flash_partition == NULL) || (region == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    region->count = 0;
    region->writable = true;

    const uint32_t capacity = region->size / sizeof(flash_store_measurement_record_t);
    for (uint32_t index = 0; index < capacity; ++index) {
        flash_store_measurement_record_t record;
        const size_t offset = region->offset + (index * sizeof(record));
        ESP_RETURN_ON_ERROR(
            esp_partition_read(s_flash_partition, offset, &record, sizeof(record)),
            TAG,
            "Failed to scan %s record %" PRIu32,
            region->name,
            index);

        if (flash_store_is_empty_record(&record)) {
            ESP_LOGI(TAG, "%s log: %" PRIu32 "/%" PRIu32 " records used", region->name, region->count, capacity);
            return ESP_OK;
        }

        if (!flash_store_record_is_valid(&record)) {
            region->writable = false;
            ESP_LOGW(
                TAG,
                "%s log: invalid non-empty record at index %" PRIu32 "; clear this region before appending",
                region->name,
                index);
            return ESP_OK;
        }

        region->count++;
    }

    region->writable = false;
    ESP_LOGI(TAG, "%s log is full: %" PRIu32 " records", region->name, capacity);
    return ESP_OK;
}

static esp_err_t flash_store_measurement_validate(const flash_store_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((measurement->unix_time_ms == 0U) ||
        (measurement->value_u6 > FLASH_STORE_MEASUREMENT_VALUE_U6_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t flash_store_region_append(
    flash_store_measurement_region_t *region,
    const flash_store_measurement_t *measurement)
{
    ESP_RETURN_ON_ERROR(flash_store_measurement_validate(measurement), TAG, "Invalid measurement");

    if ((s_flash_partition == NULL) || (region == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!region->writable) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t capacity = region->size / sizeof(flash_store_measurement_record_t);
    if (region->count >= capacity) {
        region->writable = false;
        return ESP_ERR_NO_MEM;
    }

    flash_store_measurement_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = FLASH_STORE_RECORD_MAGIC;
    record.unix_time_ms = measurement->unix_time_ms;
    record.value_u6 = measurement->value_u6;
    record.sequence = region->count + 1U;
    record.reserved = FLASH_STORE_RECORD_VERSION;
    record.crc32 = flash_store_record_crc(&record);

    const size_t offset = region->offset + (region->count * sizeof(record));
    ESP_RETURN_ON_ERROR(
        esp_partition_write(s_flash_partition, offset, &record, sizeof(record)),
        TAG,
        "Failed to append %s measurement",
        region->name);

    region->count++;
    return ESP_OK;
}

static esp_err_t flash_store_region_read(
    const flash_store_measurement_region_t *region,
    uint32_t index,
    flash_store_measurement_t *measurement)
{
    if ((s_flash_partition == NULL) || (region == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index >= region->count) {
        return ESP_ERR_NOT_FOUND;
    }

    flash_store_measurement_record_t record;
    const size_t offset = region->offset + (index * sizeof(record));
    ESP_RETURN_ON_ERROR(
        esp_partition_read(s_flash_partition, offset, &record, sizeof(record)),
        TAG,
        "Failed to read %s measurement %" PRIu32,
        region->name,
        index);

    if (!flash_store_record_is_valid(&record)) {
        return ESP_ERR_INVALID_CRC;
    }

    measurement->unix_time_ms = record.unix_time_ms;
    measurement->value_u6 = record.value_u6;
    return ESP_OK;
}

static esp_err_t flash_store_region_count(
    const flash_store_measurement_region_t *region,
    uint32_t *count)
{
    if (region == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = region->count;
    return ESP_OK;
}

static esp_err_t flash_store_region_clear(flash_store_measurement_region_t *region)
{
    if ((s_flash_partition == NULL) || (region == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        esp_partition_erase_range(s_flash_partition, region->offset, region->size),
        TAG,
        "Failed to erase %s measurement region",
        region->name);

    region->count = 0;
    region->writable = true;
    return ESP_OK;
}

esp_err_t flash_store_init(void)
{
    if (s_flash_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(
        TAG,
        "Initializing external SPI flash on host=%d MOSI=%d MISO=%d SCLK=%d CS=%d",
        FLASH_STORE_SPI_HOST,
        FLASH_STORE_SPI_MOSI_GPIO,
        FLASH_STORE_SPI_MISO_GPIO,
        FLASH_STORE_SPI_SCLK_GPIO,
        FLASH_STORE_SPI_CS_GPIO);

    const spi_bus_config_t bus_config = {
        .mosi_io_num = FLASH_STORE_SPI_MOSI_GPIO,
        .miso_io_num = FLASH_STORE_SPI_MISO_GPIO,
        .sclk_io_num = FLASH_STORE_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    esp_err_t err = spi_bus_initialize(FLASH_STORE_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus already initialized, reusing host=%d", FLASH_STORE_SPI_HOST);
    }

    const esp_flash_spi_device_config_t flash_config = {
        .host_id = FLASH_STORE_SPI_HOST,
        .cs_io_num = FLASH_STORE_SPI_CS_GPIO,
        .io_mode = SPI_FLASH_DIO,
        .input_delay_ns = 0,
        .cs_id = 0,
        .freq_mhz = FLASH_STORE_SPI_FREQ_MHZ,
        .clock_source = SPI_CLK_SRC_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(
        spi_bus_add_flash_device(&s_flash_chip, &flash_config),
        TAG,
        "Failed to add external SPI flash device");

    ESP_RETURN_ON_ERROR(
        esp_flash_init(s_flash_chip),
        TAG,
        "Failed to initialize external SPI flash chip");

    uint32_t flash_id = 0;
    ESP_RETURN_ON_ERROR(
        esp_flash_read_id(s_flash_chip, &flash_id),
        TAG,
        "Failed to read external SPI flash ID");

    uint32_t physical_size = 0;
    ESP_RETURN_ON_ERROR(
        esp_flash_get_physical_size(s_flash_chip, &physical_size),
        TAG,
        "Failed to read external SPI flash physical size");

    s_flash_initialized = true;
    ESP_LOGI(
        TAG,
        "External SPI flash detected: id=0x%06" PRIX32 ", physical_size=%" PRIu32 " bytes (%" PRIu32 " Mbit)",
        flash_id,
        physical_size,
        (physical_size * 8U) / (1024U * 1024U));

    ESP_RETURN_ON_ERROR(flash_store_validate_layout(physical_size), TAG, "Invalid flash layout");

    ESP_RETURN_ON_ERROR(
        esp_partition_register_external(
            s_flash_chip,
            0,
            FLASH_STORE_TOTAL_RESERVED_BYTES,
            "ext_store",
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
            &s_flash_partition),
        TAG,
        "Failed to register external flash partition");

    ESP_RETURN_ON_ERROR(flash_store_scan_region(&s_voltage_region), TAG, "Failed to scan voltage region");
    ESP_RETURN_ON_ERROR(flash_store_scan_region(&s_current_region), TAG, "Failed to scan current region");

    return ESP_OK;
}

esp_err_t flash_store_voltage_append(const flash_store_measurement_t *measurement)
{
    return flash_store_region_append(&s_voltage_region, measurement);
}

esp_err_t flash_store_current_append(const flash_store_measurement_t *measurement)
{
    return flash_store_region_append(&s_current_region, measurement);
}

esp_err_t flash_store_voltage_read(uint32_t index, flash_store_measurement_t *measurement)
{
    return flash_store_region_read(&s_voltage_region, index, measurement);
}

esp_err_t flash_store_current_read(uint32_t index, flash_store_measurement_t *measurement)
{
    return flash_store_region_read(&s_current_region, index, measurement);
}

esp_err_t flash_store_voltage_count(uint32_t *count)
{
    return flash_store_region_count(&s_voltage_region, count);
}

esp_err_t flash_store_current_count(uint32_t *count)
{
    return flash_store_region_count(&s_current_region, count);
}

esp_err_t flash_store_voltage_clear(void)
{
    return flash_store_region_clear(&s_voltage_region);
}

esp_err_t flash_store_current_clear(void)
{
    return flash_store_region_clear(&s_current_region);
}
