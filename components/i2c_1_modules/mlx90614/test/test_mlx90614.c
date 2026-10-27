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

#include "mlx90614_module.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/task.h"
#include "mlx90614.h"
/* Compile-time regression: the vendor header must tolerate repeated inclusion. */
#include "mlx90614.h"
#include "sdkconfig.h"
#include "unity.h"

static char s_text[128];
static uint8_t s_binary[MLX90614_RESPONSE_BYTES];
static size_t s_length;
static mlx90614_state_t *s_callback_state;

static void capture_text(const char *text)
{
    snprintf(s_text, sizeof(s_text), "%s", text);
}

static void capture_binary(const uint8_t *data, size_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(s_binary), length);
    /* Transport callbacks must never hold the acquisition/history lock. */
    TEST_ASSERT_NOT_EQUAL((uintptr_t)xTaskGetCurrentTaskHandle(),
        (uintptr_t)xSemaphoreGetMutexHolder(s_callback_state->history_lock));
    memcpy(s_binary, data, length);
    s_length = length;
}

static const scpi_handler_response_writer_t s_writer = {
    .write_text = capture_text,
    .write_binary = capture_binary,
};

static void prepare(mlx90614_state_t *state)
{
    memset(state, 0, sizeof(*state));
    s_text[0] = '\0';
    s_length = 0;
    state->history_lock = xSemaphoreCreateMutex();
    state->response_lock = xSemaphoreCreateMutex();
    state->history.samples = heap_caps_calloc(MLX90614_HISTORY_CAPACITY,
        sizeof(mlx90614_sample_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    state->response = heap_caps_malloc(MLX90614_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(state->history_lock);
    TEST_ASSERT_NOT_NULL(state->response_lock);
    TEST_ASSERT_NOT_NULL(state->history.samples);
    TEST_ASSERT_NOT_NULL(state->response);
    TEST_ASSERT_TRUE(esp_ptr_external_ram(state->history.samples));
    TEST_ASSERT_TRUE(esp_ptr_external_ram(state->response));
    state->ready = true;
    s_callback_state = state;
}

static void finish(mlx90614_state_t *state)
{
    heap_caps_free(state->history.samples);
    heap_caps_free(state->response);
    vSemaphoreDelete(state->history_lock);
    vSemaphoreDelete(state->response_lock);
    s_callback_state = NULL;
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

TEST_CASE("pyrometer conversion and scalar queries retain signed temperatures", "[mlx90614]")
{
    int32_t temperature;
    TEST_ASSERT_TRUE(mlx90614_decode_temperature(13000, &temperature));
    TEST_ASSERT_EQUAL_INT32(-131500, temperature);
    TEST_ASSERT_FALSE(mlx90614_decode_temperature(0x8000, &temperature));
    TEST_ASSERT_FALSE(mlx90614_decode_temperature(0x7fff, NULL));
    TEST_ASSERT_TRUE(mlx90614_decode_temperature(15000, &temperature));
    TEST_ASSERT_EQUAL_INT32(268500, temperature);

    mlx90614_state_t state;
    prepare(&state);
    const int32_t values[] = {-131500, 0, 268500, INT32_MIN};
    const char *expected[] = {"-13.1500", "0.0000", "26.8500", "-214748.3648"};
    for (unsigned i = 0; i < 4; ++i) {
        state.history = (mlx90614_history_t){.samples = state.history.samples};
        mlx90614_history_append(&state.history, (mlx90614_sample_t){i, values[i]});
        mlx90614_query_temperature(i % 2 ? "1" : "ch1", &s_writer, &state);
        TEST_ASSERT_EQUAL_STRING(expected[i], s_text);
    }
    state.history.latest_valid = false;
    mlx90614_query_temperature("CH1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);
    state.history = (mlx90614_history_t){.samples = state.history.samples};
    mlx90614_history_append(&state.history, (mlx90614_sample_t){5, -100});
    mlx90614_query_temperature("CH1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING("-0.0100", s_text);
    finish(&state);
}

TEST_CASE("pyrometer scalar averages newest configured polls across ring wrap", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    mlx90614_query_temperature("1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);

    const size_t window = CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT;
    for (size_t i = 0; i < MLX90614_HISTORY_CAPACITY + window; ++i) {
        mlx90614_history_append(&state.history, (mlx90614_sample_t){i, (int32_t)i * 200});
        /* Check partial windows at startup as well as the full, wrapped ring. */
        if (i < window || i == MLX90614_HISTORY_CAPACITY + window - 1) {
            const size_t count = i + 1 < window ? i + 1 : window;
            const int32_t expected = (int32_t)(2 * i - (count - 1)) * 100;
            char response[24];
            snprintf(response, sizeof(response), "%ld.%04ld",
                     (long)(expected / 10000), (long)(expected % 10000));
            mlx90614_query_temperature("CH1", &s_writer, &state);
            TEST_ASSERT_EQUAL_STRING(response, s_text);
            /* Repeated queries must not add samples or change the window. */
            mlx90614_query_temperature("1", &s_writer, &state);
            TEST_ASSERT_EQUAL_STRING(response, s_text);
        }
    }
    mlx90614_query_history("1,1,999", &s_writer, &state);
    TEST_ASSERT_EQUAL_HEX32((MLX90614_HISTORY_CAPACITY + window - 1) * 200,
                          read_u32(s_binary + 7));
    finish(&state);
}

TEST_CASE("pyrometer averages signed values with rounding and a wide accumulator", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    const size_t window = CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT;
    const int32_t values[] = {INT32_MAX, INT32_MIN};
    const char *expected[] = {"214748.3647", "-214748.3648"};
    for (size_t j = 0; j < 2; ++j) {
        for (size_t i = 0; i < window; ++i) {
            mlx90614_history_append(&state.history, (mlx90614_sample_t){i, values[j]});
        }
        mlx90614_query_temperature("1", &s_writer, &state);
        TEST_ASSERT_EQUAL_STRING(expected[j], s_text);
    }
    /* Opposite signs cancel before rounding; an odd leftover sample is zero. */
    for (size_t i = 0; i < window; ++i) {
        const int32_t value = i == window - 1 && window % 2 != 0 ? 0 :
            (i % 2 == 0 ? 10000 : -10000);
        mlx90614_history_append(&state.history, (mlx90614_sample_t){i, value});
    }
    mlx90614_query_temperature("1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING("0.0000", s_text);
    /* Even windows hit the rounding midpoint; odd windows fall just below it. */
    for (int32_t sign = -1; sign <= 1; sign += 2) {
        for (size_t i = 0; i < window; ++i) {
            const int32_t value = i == 0 ? sign * (int32_t)(window + window / 2) : 0;
            mlx90614_history_append(&state.history, (mlx90614_sample_t){i, value});
        }
        mlx90614_query_temperature("1", &s_writer, &state);
        const char *rounded = window % 2 == 0 ?
            (sign < 0 ? "-0.0002" : "0.0002") :
            (sign < 0 ? "-0.0001" : "0.0001");
        TEST_ASSERT_EQUAL_STRING(rounded, s_text);
    }
    finish(&state);
}

TEST_CASE("pyrometer binary history is signed little endian oldest first and bounded", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    mlx90614_query_history("1", &s_writer, &state);
    TEST_ASSERT_EQUAL(3, s_length);
    TEST_ASSERT_EQUAL_MEMORY("#10", s_binary, 3);
    for (unsigned i = 0; i < MLX90614_HISTORY_CAPACITY + 7; ++i) {
        mlx90614_history_append(&state.history, (mlx90614_sample_t){i, -(int32_t)i * 100});
    }
    TEST_ASSERT_EQUAL(MLX90614_HISTORY_CAPACITY, state.history.count);
    mlx90614_query_history("CH1,2,3", &s_writer, &state);
    TEST_ASSERT_EQUAL(20, s_length);
    TEST_ASSERT_EQUAL_MEMORY("#216", s_binary, 4);
    TEST_ASSERT_EQUAL_UINT32(10, read_u32(s_binary + 4));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)-1000, read_u32(s_binary + 8));
    TEST_ASSERT_EQUAL_UINT32(11, read_u32(s_binary + 12));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)-1100, read_u32(s_binary + 16));
    mlx90614_query_history("1,9999", &s_writer, &state);
    TEST_ASSERT_EQUAL(8006, s_length);
    TEST_ASSERT_EQUAL_MEMORY("#48000", s_binary, 6);
    mlx90614_query_history("1,0", &s_writer, &state);
    TEST_ASSERT_EQUAL(3, s_length);
    mlx90614_query_history("1,10,1000", &s_writer, &state);
    TEST_ASSERT_EQUAL(3, s_length);
    /* A runtime error invalidates the scalar but does not destroy history. */
    state.history.latest_valid = false;
    mlx90614_query_history("1,1", &s_writer, &state);
    TEST_ASSERT_EQUAL(11, s_length);
    finish(&state);
}

TEST_CASE("pyrometer commands reject malformed inputs and unavailable states", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    const char *invalid[] = {NULL, "", "C", "CH", "0", "CH0", "2", "CH2", "+1", "01", "1x",
        "1,", "1,-1", "1,2,", "1,2,3,4", "1,2x", "1,184467440737095516160"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        s_text[0] = '\0';
        mlx90614_query_history(invalid[i], &s_writer, &state);
        TEST_ASSERT_NOT_NULL(strstr(s_text, "ERR,"));
    }
    mlx90614_query_temperature("1,2", &s_writer, &state);
    TEST_ASSERT_NOT_NULL(strstr(s_text, "ERR,"));
    scpi_handler_response_writer_t text_only = {.write_text = capture_text};
    mlx90614_query_history("1", &text_only, &state);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Binary response not supported\"", s_text);
    state.ready = false;
    mlx90614_query_history("1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);
    finish(&state);

    /* Startup failure leaves registered commands reachable but unavailable. */
    TEST_ASSERT_EQUAL(ESP_OK, mlx90614_module_register_commands());
    const char *aliases[] = {"EXT:MEAS:TEMP?", "EXT:MEASURE:TEMP?",
        "EXT:MEAS:TEMPERATURE?", "EXT:MEASURE:TEMPERATURE?",
        "EXT:MEAS:TEMP:DATA?", "EXT:MEASURE:TEMP:DATA?",
        "EXT:MEAS:TEMPERATURE:DATA?", "EXT:MEASURE:TEMPERATURE:DATA?"};
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        char command[80];
        snprintf(command, sizeof(command), "%s CH1", aliases[i]);
        scpi_handler_handle_command(command, &s_writer);
        TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);
    }
}

TEST_CASE("pyrometer failed reads preserve history and successful reads recover", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    TEST_ASSERT_EQUAL(ESP_OK, mlx90614_store_result(&state, ESP_OK, 13000, UINT32_MAX));
    TEST_ASSERT_EQUAL(1, state.history.count);
    const esp_err_t errors[] = {ESP_ERR_INVALID_CRC, ESP_ERR_TIMEOUT, ESP_ERR_NOT_FOUND};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        TEST_ASSERT_EQUAL(errors[i], mlx90614_store_result(&state, errors[i], 15000, i));
        TEST_ASSERT_FALSE(state.history.latest_valid);
        TEST_ASSERT_EQUAL(1, state.history.count);
        mlx90614_query_temperature("1", &s_writer, &state);
        TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      mlx90614_store_result(&state, ESP_OK, 0xffff, 1));
    TEST_ASSERT_EQUAL(1, state.history.count);
    TEST_ASSERT_EQUAL(ESP_OK, mlx90614_store_result(&state, ESP_OK, 15000, 2));
    TEST_ASSERT_TRUE(state.history.latest_valid);
    TEST_ASSERT_EQUAL(2, state.history.count);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, mlx90614_history_at(&state.history, 0)->time_ms);
    TEST_ASSERT_EQUAL_UINT32(2, mlx90614_history_at(&state.history, 1)->time_ms);
    mlx90614_query_temperature("1", &s_writer, &state);
    TEST_ASSERT_EQUAL_STRING(CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT == 1 ?
                             "26.8500" : "6.8500", s_text);
    finish(&state);
}

typedef struct {
    mlx90614_state_t *state;
    SemaphoreHandle_t done;
} producer_args_t;

static void produce_samples(void *context)
{
    producer_args_t *args = context;
    for (unsigned i = 0; i < 2000; ++i) {
        mlx90614_store_result(args->state, ESP_OK, 13000, i);
        if (i % 16 == 0) {
            vTaskDelay(1);
        }
    }
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

TEST_CASE("pyrometer sampling and binary queries use independent locks", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(done);
    producer_args_t args = {&state, done};
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(produce_samples, "temp_test", 4096, &args, 4, NULL));
    for (unsigned i = 0; i < 100; ++i) {
        mlx90614_query_history("CH1,100", &s_writer, &state);
        TEST_ASSERT_TRUE(s_length >= 3 && s_length <= 805);
        vTaskDelay(1);
    }
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(5000)));
    vSemaphoreDelete(done);
    finish(&state);
}

typedef struct {
    SemaphoreHandle_t entered;
    SemaphoreHandle_t release;
    SemaphoreHandle_t done;
    bool coherent;
    bool released;
} stalled_response_t;

static stalled_response_t s_stalled;

static void stall_binary(const uint8_t *data, size_t length)
{
    uint8_t snapshot[11];  /* One sample plus the #18 header. */
    s_stalled.coherent = length == sizeof(snapshot);
    if (s_stalled.coherent) {
        memcpy(snapshot, data, length);
    }
    xSemaphoreGive(s_stalled.entered);
    /* Bound even a broken implementation so the test reports instead of hanging. */
    s_stalled.released = xSemaphoreTake(s_stalled.release, pdMS_TO_TICKS(2000)) == pdTRUE;
    if (s_stalled.coherent) {
        s_stalled.coherent = memcmp(snapshot, data, length) == 0;
    }
}

static void query_with_stalled_writer(void *context)
{
    const scpi_handler_response_writer_t writer = {
        .write_text = capture_text,
        .write_binary = stall_binary,
    };
    mlx90614_query_history("1,1", &writer, context);
    xSemaphoreGive(s_stalled.done);
    vTaskDelete(NULL);
}

TEST_CASE("pyrometer busy history does not block sampling or scalar queries", "[mlx90614]")
{
    mlx90614_state_t state;
    prepare(&state);
    mlx90614_store_result(&state, ESP_OK, 13000, 1);
    s_stalled = (stalled_response_t){
        .entered = xSemaphoreCreateBinary(),
        .release = xSemaphoreCreateBinary(),
        .done = xSemaphoreCreateBinary(),
    };
    TEST_ASSERT_NOT_NULL(s_stalled.entered);
    TEST_ASSERT_NOT_NULL(s_stalled.release);
    TEST_ASSERT_NOT_NULL(s_stalled.done);
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(query_with_stalled_writer, "temp_stall", 4096,
                                        &state, 4, NULL));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(s_stalled.entered, pdMS_TO_TICKS(1000)));
    const esp_err_t stored = mlx90614_store_result(&state, ESP_OK, 15000, 2);
    mlx90614_query_temperature("1", &s_writer, &state);
    const bool scalar_ok = strcmp(s_text, CONFIG_PSUEXT_MLX90614_AVERAGE_COUNT == 1 ?
                                  "26.8500" : "6.8500") == 0;
    mlx90614_query_history("1", &s_writer, &state);
    const bool busy = strcmp(s_text, "ERR,\"Temperature history busy\"") == 0;
    const bool no_binary = s_length == 0;
    xSemaphoreGive(s_stalled.release);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(s_stalled.done, pdMS_TO_TICKS(3000)));
    const bool released = s_stalled.released;
    const bool coherent = s_stalled.coherent;
    vSemaphoreDelete(s_stalled.entered);
    vSemaphoreDelete(s_stalled.release);
    vSemaphoreDelete(s_stalled.done);
    mlx90614_query_history("1", &s_writer, &state);
    const bool recovered = s_length == 20 && read_u32(s_binary + 12) == 2;
    finish(&state);
    TEST_ASSERT_EQUAL(ESP_OK, stored);
    TEST_ASSERT_TRUE(scalar_ok);
    TEST_ASSERT_TRUE(busy);
    TEST_ASSERT_TRUE(no_binary);
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_TRUE(coherent);
    TEST_ASSERT_TRUE(recovered);
}

/* Fault injection is restricted to a sentinel bus and PSRAM allocations during
 * this case; unrelated component tests continue using their real drivers. */
static bool s_fail_psram;
static unsigned s_sensor_cleanup_count;
static unsigned s_sensor_init_count;

esp_err_t __real_i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout);
esp_err_t __wrap_i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    if (bus == (i2c_master_bus_handle_t)(uintptr_t)1) {
        TEST_ASSERT_EQUAL(0x5a, address);
        return ESP_OK;
    }
    return __real_i2c_master_probe(bus, address, timeout);
}

esp_err_t __real_mlx90614_init(i2c_master_bus_handle_t bus, const mlx90614_config_t *config,
                              mlx90614_handle_t *sensor);
esp_err_t __wrap_mlx90614_init(i2c_master_bus_handle_t bus, const mlx90614_config_t *config,
                              mlx90614_handle_t *sensor)
{
    if (bus == (i2c_master_bus_handle_t)(uintptr_t)1) {
        TEST_ASSERT_EQUAL(100000, config->mlx90614_device.scl_speed_hz);
        *sensor = (mlx90614_handle_t)(uintptr_t)1;
        ++s_sensor_init_count;
        return ESP_OK;
    }
    return __real_mlx90614_init(bus, config, sensor);
}

esp_err_t __real_mlx90614_deinit(mlx90614_handle_t sensor);
esp_err_t __wrap_mlx90614_deinit(mlx90614_handle_t sensor)
{
    if (sensor == (mlx90614_handle_t)(uintptr_t)1) {
        ++s_sensor_cleanup_count;
        return ESP_OK;
    }
    return __real_mlx90614_deinit(sensor);
}

void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__wrap_heap_caps_malloc(size_t size, uint32_t caps)
{
    if (s_fail_psram && (caps & MALLOC_CAP_SPIRAM)) {
        return NULL;
    }
    return __real_heap_caps_malloc(size, caps);
}

TEST_CASE("pyrometer PSRAM allocation failure cleans up and caches startup failure", "[mlx90614]")
{
    /* Register first so lock allocation is independent of fault injection. */
    TEST_ASSERT_EQUAL(ESP_OK, mlx90614_module_register_commands());
    s_fail_psram = true;
    esp_err_t err = mlx90614_module_init((i2c_master_bus_handle_t)(uintptr_t)1);
    s_fail_psram = false;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);
    TEST_ASSERT_EQUAL(1, s_sensor_init_count);
    TEST_ASSERT_EQUAL(1, s_sensor_cleanup_count);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,
                      mlx90614_module_init((i2c_master_bus_handle_t)(uintptr_t)1));
    TEST_ASSERT_EQUAL(1, s_sensor_init_count);
    char command[] = "EXT:MEAS:TEMP? CH1";
    scpi_handler_handle_command(command, &s_writer);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Temperature unavailable\"", s_text);
}
