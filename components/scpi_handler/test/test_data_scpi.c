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
