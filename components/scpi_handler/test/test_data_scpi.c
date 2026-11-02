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

#include "scpi_handler.h"
#include "measure_svc.h"
#include "measure_svc_samples.h"
#include "measure_svc_precision.h"
#include "../../measure_svc/measure_svc_event_bus.h"
#include <stdio.h>
#include "scpi_test_fixture.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "unity.h"
#include <stdatomic.h>
#include <string.h>

typedef struct {
    SemaphoreHandle_t ready;
    SemaphoreHandle_t start;
    SemaphoreHandle_t done;
} data_task_t;
static atomic_uint s_data_blocks;
static atomic_uint s_data_errors;

static void data_text(const char *text)
{
    (void)text;
    atomic_fetch_add(&s_data_errors, 1);
}

static void data_binary(const uint8_t *data, size_t length)
{
    /* Zero requested records must always produce the definite-length empty block. */
    if (length != 3 || memcmp(data, "#10", 3) != 0) {
        atomic_fetch_add(&s_data_errors, 1);
    }
    /* Yield while the shared response is borrowed, exercising writer serialization. */
    vTaskDelay(1);
    if (length != 3 || memcmp(data, "#10", 3) != 0) {
        atomic_fetch_add(&s_data_errors, 1);
    }
    atomic_fetch_add(&s_data_blocks, 1);
}

static void query_data(void *context)
{
    data_task_t *task = context;
    const scpi_handler_response_writer_t writer = {
        .write_text = data_text,
        .write_binary = data_binary,
    };
    xSemaphoreGive(task->ready);
    xSemaphoreTake(task->start, portMAX_DELAY);
    for (unsigned i = 0; i < 8; ++i) {
        char command[] = "MEAS:VOLT:DATA? CH1,0";
        scpi_handler_handle_command(command, &writer);
    }
    xSemaphoreGive(task->done);
    vTaskDelete(NULL);
}

TEST_CASE("concurrent DATA queries serialize buffers including first use", "[scpi][data][concurrency]")
{
    ensure_runtime();
    atomic_store(&s_data_blocks, 0);
    atomic_store(&s_data_errors, 0);
    data_task_t task = {
        .ready = xSemaphoreCreateCounting(2, 0),
        .start = xSemaphoreCreateCounting(2, 0),
        .done = xSemaphoreCreateCounting(2, 0),
    };
    TEST_ASSERT_NOT_NULL(task.ready);
    TEST_ASSERT_NOT_NULL(task.start);
    TEST_ASSERT_NOT_NULL(task.done);
    unsigned created = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (xTaskCreate(query_data, "data_query", 4096, &task, 4, NULL) == pdPASS) {
            ++created;
        }
    }
    for (unsigned i = 0; i < created; ++i) {
        xSemaphoreTake(task.ready, portMAX_DELAY);
    }
    for (unsigned i = 0; i < created; ++i) {
        xSemaphoreGive(task.start);
    }
    for (unsigned i = 0; i < created; ++i) {
        xSemaphoreTake(task.done, portMAX_DELAY);
    }
    vSemaphoreDelete(task.ready);
    vSemaphoreDelete(task.start);
    vSemaphoreDelete(task.done);
    TEST_ASSERT_EQUAL_UINT32(2, created);
    TEST_ASSERT_EQUAL_UINT32(0, atomic_load(&s_data_errors));
    TEST_ASSERT_EQUAL_UINT32(16, atomic_load(&s_data_blocks));
}

static uint8_t s_captured_data[16U + 8U * MEASURE_SVC_MAX_SAMPLE_CAPACITY];
static size_t s_captured_length;

static void capture_data(const uint8_t *data, size_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(s_captured_data), length);
    memcpy(s_captured_data, data, length);
    s_captured_length = length;
}

static void unexpected_text(const char *text)
{
    TEST_FAIL_MESSAGE(text);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static const uint8_t *run_data(const char *query, size_t count)
{
    char command[96];
    strlcpy(command, query, sizeof(command));
    s_captured_length = 0U;
    const scpi_handler_response_writer_t writer = {
        .write_text = unexpected_text, .write_binary = capture_data,
    };
    scpi_handler_handle_command(command, &writer);
    char header[16];
    char size[12];
    snprintf(size, sizeof(size), "%u", (unsigned)(count * 8U));
    const int header_length = snprintf(header, sizeof(header), "#%u%s", (unsigned)strlen(size), size);
    TEST_ASSERT_EQUAL_UINT32(header_length + count * 8U, s_captured_length);
    TEST_ASSERT_EQUAL_MEMORY(header, s_captured_data, header_length);
    return s_captured_data + header_length;
}

static void publish_data(measure_channel_t channel, measure_kind_t kind, double value, uint32_t time_ms)
{
    const measure_svc_sample_event_t event = {
        .channel = channel, .kind = kind, .time_ms = time_ms,
        .precise_u4 = value, .value_u4 = measure_svc_precision_to_u4(value),
    };
    measure_svc_event_bus_publish(&event);
}

TEST_CASE("DATA applies current REL to wrapped voltage and current history", "[rel][scpi][data]")
{
    ensure_runtime();
    const measure_channel_t channels[] = {MEASURE_CHANNEL_0, MEASURE_CHANNEL_1, MEASURE_CHANNEL_1};
    const measure_kind_t kinds[] = {MEASURE_KIND_VOLTAGE, MEASURE_KIND_VOLTAGE, MEASURE_KIND_CURRENT};
    const char *queries[] = {"MEAS:VOLT:DATA? CH0,3,0", "MEAS:VOLT:DATA? CH1,3,0", "MEAS:CURR:DATA? CH1,3,0"};
    const uint32_t offsets[] = {2U, 3U, 4U};
    for (unsigned target = 0; target < 3; ++target) {
        TEST_ESP_OK(measure_svc_set_relative_u4(channels[target], kinds[target], offsets[target]));
        for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY + 17U; ++i) {
            publish_data(channels[target], kinds[target], (double)(i % 3U + 2U), i);
        }
    }
    for (unsigned target = 0; target < 3; ++target) {
        const uint8_t *records = run_data(queries[target], 3U);
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t raw = (17U + i) % 3U + 2U;
            TEST_ASSERT_EQUAL_UINT32(17U + i, read_u32_le(records + 8U * i));
            TEST_ASSERT_EQUAL_UINT32(raw > offsets[target] ? raw - offsets[target] : 0U,
                read_u32_le(records + 8U * i + 4U));
        }
        measure_svc_sample_t absolute[3];
        size_t copied;
        esp_err_t err = kinds[target] == MEASURE_KIND_CURRENT ?
            measure_svc_copy_current_samples(channels[target], 0U, 3U, absolute, &copied) :
            measure_svc_copy_voltage_samples(channels[target], 0U, 3U, absolute, &copied);
        TEST_ESP_OK(err);
        TEST_ASSERT_EQUAL_UINT32(3U, copied);
        TEST_ASSERT_EQUAL_UINT32(4U, absolute[0].value_u4);
        TEST_ESP_OK(measure_svc_set_relative_u4(channels[target], kinds[target], 0U));
        records = run_data(queries[target], 3U);
        TEST_ASSERT_EQUAL_UINT32(4U, read_u32_le(records + 4U));
    }
    const uint8_t *last = run_data("MEAS:VOLT:DATA? CH0,3,999", 1U);
    TEST_ASSERT_EQUAL_UINT32(1016U, read_u32_le(last));
    run_data("MEAS:CURR:DATA? CH1,2,1000", 0U);
    run_data("MEAS:VOLT:DATA? CH0,0", 0U);
}

TEST_CASE("power queries apply REL to original precise pairs before averaging", "[rel][scpi][data][precision]")
{
    ensure_runtime();
    uint32_t previous_count;
    TEST_ESP_OK(measure_svc_get_average_count(MEASURE_KIND_POWER, &previous_count));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_POWER, 2U));
    /* Force ring wrap and settle any pending pair from earlier tests. */
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0.0, 0U);
    for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY + 2U; ++i) {
        publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.25, 2U * i);
        publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 20000.5, 2U * i + 1U);
    }
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 30000.75, 3000U);
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 40000.25, 3001U);
    /* Overwrite voltage history without producing another pair. Power must retain
     * its original inputs even when the independent input ring no longer does. */
    for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY; ++i) {
        publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 90000.0, 4000U + i);
    }
    const uint32_t voltage_offsets[] = {0U, 10000U, 0U, 10000U, 40000U, 0U, 0U};
    const uint32_t current_offsets[] = {0U, 0U, 20000U, 20000U, 0U, 50000U, 0U};
    const uint32_t expected[][2] = {{20001U, 120004U}, {1U, 80004U}, {1U, 60002U},
        {0U, 40002U}, {0U, 0U}, {0U, 0U}, {20001U, 120004U}};
    const char *averages[] = {"7.0002", "4.0002", "3.0001", "2.0001", "0.0000", "0.0000", "7.0002"};
    for (unsigned i = 0; i < 7U; ++i) {
        TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, voltage_offsets[i]));
        TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, current_offsets[i]));
        const uint8_t *records = run_data("MEAS:POWER:DATA? CH1,2,998", 2U);
        TEST_ASSERT_EQUAL_UINT32(expected[i][0], read_u32_le(records + 4U));
        TEST_ASSERT_EQUAL_UINT32(expected[i][1], read_u32_le(records + 12U));
        TEST_ASSERT_EQUAL_UINT32(3001U, read_u32_le(records + 8U));
        char query[] = "MEAS:POWER? CH1";
        run_scpi(query);
        TEST_ASSERT_EQUAL_STRING(averages[i], s_last_response);
    }
    measure_svc_sample_t absolute[2];
    size_t copied;
    TEST_ESP_OK(measure_svc_copy_power_samples(MEASURE_CHANNEL_1, 998U, 2U, absolute, &copied));
    TEST_ASSERT_EQUAL_UINT32(2U, copied);
    TEST_ASSERT_EQUAL_UINT32(20001U, absolute[0].value_u4);
    TEST_ASSERT_EQUAL_UINT32(120004U, absolute[1].value_u4);
    /* Zero offsets preserve signed products; active REL clamps each input. */
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 0.0, 6000U);
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, -10000.0, 6001U);
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, -2.0, 6002U);
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 10000.0, 6003U);
    publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_CURRENT, 2.0, 6004U);
    const uint8_t *records = run_data("MEAS:POWER:DATA? CH1,2,998", 2U);
    TEST_ASSERT_EQUAL_UINT32(2U, read_u32_le(records + 4U));
    TEST_ASSERT_EQUAL_UINT32(2U, read_u32_le(records + 12U));
    uint32_t value;
    TEST_ESP_OK(measure_svc_read_relative(MEASURE_CHANNEL_1, MEASURE_KIND_POWER, &value));
    TEST_ASSERT_EQUAL_UINT32(2U, value);
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 1U));
    records = run_data("MEAS:POWER:DATA? CH1,2,998", 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, read_u32_le(records + 4U));
    TEST_ASSERT_EQUAL_UINT32(2U, read_u32_le(records + 12U));
    TEST_ESP_OK(measure_svc_read_relative(MEASURE_CHANNEL_1, MEASURE_KIND_POWER, &value));
    TEST_ASSERT_EQUAL_UINT32(1U, value);
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 0U));
    TEST_ESP_OK(measure_svc_set_average_count(MEASURE_KIND_POWER, previous_count));
}

static atomic_bool s_change_rel;
static atomic_uint s_rel_errors;

static void change_rel(void *context)
{
    SemaphoreHandle_t done = context;
    uint32_t offset = 0U;
    while (atomic_load(&s_change_rel)) {
        if (measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, offset) != ESP_OK) {
            atomic_fetch_add(&s_rel_errors, 1U);
        }
        offset = offset == 0U ? 1000U : 0U;
        vTaskDelay(1);
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

static void capture_data_and_change_rel(const uint8_t *data, size_t length)
{
    capture_data(data, length);
    /* This would deadlock if a measurement/coordination lock reached the writer. */
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 0U));
    vTaskDelay(1);
    TEST_ASSERT_EQUAL_MEMORY(s_captured_data, data, length);
}

TEST_CASE("DATA keeps one REL snapshot while offsets change", "[rel][scpi][data][concurrency]")
{
    ensure_runtime();
    for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY; ++i) {
        publish_data(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 2000.0, i);
    }
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 0U));
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(done);
    atomic_store(&s_change_rel, true);
    atomic_store(&s_rel_errors, 0U);
    const BaseType_t created = xTaskCreate(change_rel, "change_rel", 4096, done, 4, NULL);
    TEST_ASSERT_EQUAL(pdPASS, created);
    const scpi_handler_response_writer_t writer = {
        .write_text = unexpected_text, .write_binary = capture_data_and_change_rel,
    };
    unsigned errors = 0U;
    for (unsigned iteration = 0; iteration < 16U; ++iteration) {
        char command[] = "MEAS:VOLT:DATA? CH1";
        scpi_handler_handle_command(command, &writer);
        if (s_captured_length != 8006U || memcmp(s_captured_data, "#48000", 6U) != 0) {
            ++errors;
            continue;
        }
        const uint32_t first = read_u32_le(s_captured_data + 10U);
        if (first != 1000U && first != 2000U) {
            ++errors;
        }
        for (unsigned i = 0; i < MEASURE_SVC_MAX_SAMPLE_CAPACITY; ++i) {
            if (read_u32_le(s_captured_data + 10U + 8U * i) != first) {
                ++errors;
            }
        }
    }
    atomic_store(&s_change_rel, false);
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    TEST_ESP_OK(measure_svc_set_relative_u4(MEASURE_CHANNEL_1, MEASURE_KIND_VOLTAGE, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, errors);
    TEST_ASSERT_EQUAL_UINT32(0U, atomic_load(&s_rel_errors));
}
