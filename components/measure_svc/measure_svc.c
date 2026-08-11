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
 * @file measure_svc.c
 * @brief Active measurement provider indirection for SCPI measurement commands.
 */

#include "measure_svc.h"
#include "measure_svc_internal.h"
#include "measure_svc_storage.h"

#include <inttypes.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define MEASURE_SVC_SAMPLER_TASK_STACK_WORDS 4096U
#define MEASURE_SVC_SAMPLER_TASK_PRIORITY 5U
#define MEASURE_SVC_NVS_NAMESPACE "meas_cfg"
#define MEASURE_SVC_NVS_KEY_VOLT_AVG_COUNT "volt_avg_n"
#define MEASURE_SVC_NVS_KEY_CURR_AVG_COUNT "curr_avg_n"
#define MEASURE_SVC_NVS_KEY_POWER_AVG_COUNT "power_avg_n"

static const char *TAG = "measure_svc";

static const measure_provider_t *s_active_prov;
static TaskHandle_t s_sampler_task_handle;
static SemaphoreHandle_t s_listener_lock;
static bool s_measure_svc_initialized;
static measure_svc_config_t s_measure_config;
static uint32_t s_sample_rate_hz = MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ;
static uint32_t s_voltage_average_count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
static uint32_t s_current_average_count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
static uint32_t s_power_average_count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;

typedef struct {
    measure_svc_sample_listener_fn_t callback;
    void *context;
} measure_svc_listener_t;

static measure_svc_listener_t s_voltage_listeners[MEASURE_SVC_MAX_SAMPLE_LISTENERS];
static measure_svc_listener_t s_current_listeners[MEASURE_SVC_MAX_SAMPLE_LISTENERS];
static measure_svc_listener_t s_power_listeners[MEASURE_SVC_MAX_SAMPLE_LISTENERS];
static size_t s_voltage_listener_count;
static size_t s_current_listener_count;
static size_t s_power_listener_count;
static measure_svc_sample_t s_average_samples[MEASURE_SVC_MAX_AVERAGE_COUNT];

typedef struct {
    bool valid;
    measure_svc_sample_event_t event;
} measure_svc_pending_power_input_t;

static measure_svc_pending_power_input_t s_pending_voltage_power_sample;
static measure_svc_pending_power_input_t s_pending_current_power_sample;

static void measure_svc_sampler_task(void *arg);
static void measure_svc_derived_power_listener(const measure_svc_sample_event_t *event, void *context);

static bool measure_svc_input_is_ads1115(measure_input_t input)
{
    return (input >= MEASURE_INPUT_ADS1115_AIN0) && (input <= MEASURE_INPUT_ADS1115_AIN3);
}

static esp_err_t measure_svc_validate_config(const measure_svc_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!measure_svc_input_is_ads1115(config->output_voltage_input) ||
        !measure_svc_input_is_ads1115(config->output_current_input) ||
        (config->output_voltage_input == config->output_current_input)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->input_voltage_input != MEASURE_INPUT_UNUSED) {
        if (!measure_svc_input_is_ads1115(config->input_voltage_input) ||
            (config->input_voltage_input == config->output_voltage_input) ||
            (config->input_voltage_input == config->output_current_input)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return ESP_OK;
}

esp_err_t measure_svc_get_physical_input(
    measure_channel_t channel,
    measure_kind_t kind,
    measure_input_t *input)
{
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((channel == MEASURE_CHANNEL_0) && (kind == MEASURE_KIND_VOLTAGE)) {
        if (s_measure_config.input_voltage_input == MEASURE_INPUT_UNUSED) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        *input = s_measure_config.input_voltage_input;
        return ESP_OK;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        *input = s_measure_config.output_voltage_input;
        return ESP_OK;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        *input = s_measure_config.output_current_input;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Return compact uptime milliseconds for stored sample timestamps.
 */
static uint32_t measure_svc_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static esp_err_t measure_svc_listener_kind_to_storage(
    measure_kind_t kind,
    measure_svc_listener_t **listeners,
    size_t **listener_count)
{
    if ((listeners == NULL) || (listener_count == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        *listeners = s_voltage_listeners;
        *listener_count = &s_voltage_listener_count;
        return ESP_OK;
    case MEASURE_KIND_CURRENT:
        *listeners = s_current_listeners;
        *listener_count = &s_current_listener_count;
        return ESP_OK;
    case MEASURE_KIND_POWER:
        *listeners = s_power_listeners;
        *listener_count = &s_power_listener_count;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t measure_svc_kind_to_average_slot(
    measure_kind_t kind,
    uint32_t **count,
    const char **nvs_key)
{
    if ((count == NULL) || (nvs_key == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (kind) {
    case MEASURE_KIND_VOLTAGE:
        *count = &s_voltage_average_count;
        *nvs_key = MEASURE_SVC_NVS_KEY_VOLT_AVG_COUNT;
        return ESP_OK;
    case MEASURE_KIND_CURRENT:
        *count = &s_current_average_count;
        *nvs_key = MEASURE_SVC_NVS_KEY_CURR_AVG_COUNT;
        return ESP_OK;
    case MEASURE_KIND_POWER:
        *count = &s_power_average_count;
        *nvs_key = MEASURE_SVC_NVS_KEY_POWER_AVG_COUNT;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t measure_svc_init_listener_registry(void)
{
    if (s_listener_lock == NULL) {
        s_listener_lock = xSemaphoreCreateMutex();
        if (s_listener_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static uint32_t measure_svc_clamp_average_count(uint32_t count)
{
    if (count < MEASURE_SVC_MIN_AVERAGE_COUNT) {
        return MEASURE_SVC_MIN_AVERAGE_COUNT;
    }

    if (count > MEASURE_SVC_MAX_AVERAGE_COUNT) {
        return MEASURE_SVC_MAX_AVERAGE_COUNT;
    }

    return count;
}

static esp_err_t measure_svc_store_average_count(const char *key, uint32_t count)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(MEASURE_SVC_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, key, count);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t measure_svc_load_average_count(const char *key, uint32_t *count)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(MEASURE_SVC_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    err = nvs_get_u32(handle, key, count);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = MEASURE_SVC_DEFAULT_AVERAGE_COUNT;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(err, TAG, "nvs_get_u32 failed");
    *count = measure_svc_clamp_average_count(*count);
    return ESP_OK;
}

static esp_err_t measure_svc_average_init(void)
{
    ESP_RETURN_ON_ERROR(
        measure_svc_load_average_count(MEASURE_SVC_NVS_KEY_VOLT_AVG_COUNT, &s_voltage_average_count),
        TAG,
        "loading voltage averaging count failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_load_average_count(MEASURE_SVC_NVS_KEY_CURR_AVG_COUNT, &s_current_average_count),
        TAG,
        "loading current averaging count failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_load_average_count(MEASURE_SVC_NVS_KEY_POWER_AVG_COUNT, &s_power_average_count),
        TAG,
        "loading power averaging count failed");
    return ESP_OK;
}

static void measure_svc_publish_sample_event(const measure_svc_sample_event_t *event)
{
    measure_svc_listener_t *listeners;
    size_t *listener_count;
    measure_svc_listener_t callbacks[MEASURE_SVC_MAX_SAMPLE_LISTENERS];
    size_t callback_count = 0U;

    if ((event == NULL) || (s_listener_lock == NULL)) {
        return;
    }

    if (measure_svc_listener_kind_to_storage(event->kind, &listeners, &listener_count) != ESP_OK) {
        return;
    }

    if (xSemaphoreTake(s_listener_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    callback_count = *listener_count;
    for (size_t i = 0; i < callback_count; ++i) {
        callbacks[i] = listeners[i];
    }
    xSemaphoreGive(s_listener_lock);

    for (size_t i = 0; i < callback_count; ++i) {
        if (callbacks[i].callback != NULL) {
            callbacks[i].callback(event, callbacks[i].context);
        }
    }
}

esp_err_t measure_svc_register_sample_listener(
    measure_kind_t kind,
    measure_svc_sample_listener_fn_t callback,
    void *context)
{
    measure_svc_listener_t *listeners;
    size_t *listener_count;

    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "listener callback is null");
    ESP_RETURN_ON_ERROR(measure_svc_init_listener_registry(), TAG, "initializing listener registry failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_listener_kind_to_storage(kind, &listeners, &listener_count),
        TAG,
        "invalid listener kind");

    if (xSemaphoreTake(s_listener_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < *listener_count; ++i) {
        if ((listeners[i].callback == callback) && (listeners[i].context == context)) {
            xSemaphoreGive(s_listener_lock);
            return ESP_OK;
        }
    }

    if (*listener_count >= MEASURE_SVC_MAX_SAMPLE_LISTENERS) {
        xSemaphoreGive(s_listener_lock);
        return ESP_ERR_NO_MEM;
    }

    listeners[*listener_count].callback = callback;
    listeners[*listener_count].context = context;
    (*listener_count)++;
    xSemaphoreGive(s_listener_lock);
    return ESP_OK;
}

/**
 * @brief Copy calibrated voltage history samples for logical CH0 or CH1.
 */
esp_err_t measure_svc_copy_voltage_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    ESP_RETURN_ON_FALSE(
        (channel == MEASURE_CHANNEL_0) || (channel == MEASURE_CHANNEL_1),
        ESP_ERR_INVALID_ARG,
        TAG,
        "invalid logical voltage channel");
    return measure_svc_storage_copy_samples(channel, MEASURE_KIND_VOLTAGE, start_offset, max_count, samples, copied_count);
}

/**
 * @brief Copy calibrated current history samples for logical CH1.
 */
esp_err_t measure_svc_copy_current_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    ESP_RETURN_ON_FALSE(channel == MEASURE_CHANNEL_1, ESP_ERR_INVALID_ARG, TAG, "invalid logical current channel");
    return measure_svc_storage_copy_samples(channel, MEASURE_KIND_CURRENT, start_offset, max_count, samples, copied_count);
}

/**
 * @brief Copy derived power history samples for logical CH1.
 */
esp_err_t measure_svc_copy_power_samples(
    measure_channel_t channel,
    size_t start_offset,
    size_t max_count,
    measure_svc_sample_t *samples,
    size_t *copied_count)
{
    ESP_RETURN_ON_FALSE(channel == MEASURE_CHANNEL_1, ESP_ERR_INVALID_ARG, TAG, "invalid logical power channel");
    return measure_svc_storage_copy_samples(channel, MEASURE_KIND_POWER, start_offset, max_count, samples, copied_count);
}

/**
 * @brief Read one raw value directly from the active provider.
 */
static esp_err_t measure_svc_read_raw(
    measure_input_t input,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (kind == MEASURE_KIND_POWER) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((s_active_prov == NULL) || (s_active_prov->read_raw_value_u4 == NULL)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_active_prov->read_raw_value_u4(input, kind, value_u4);
}

/**
 * @brief Convert a sample rate to a non-zero FreeRTOS delay period.
 */
static uint32_t measure_svc_clamp_sample_rate_hz(uint32_t hz)
{
    if (hz < MEASURE_SVC_MIN_SAMPLE_RATE_HZ) {
        return MEASURE_SVC_MIN_SAMPLE_RATE_HZ;
    }

    if (hz > MEASURE_SVC_MAX_SAMPLE_RATE_HZ) {
        return MEASURE_SVC_MAX_SAMPLE_RATE_HZ;
    }

    return hz;
}

/**
 * @brief Convert a sample rate to a non-zero FreeRTOS delay period.
 */
static TickType_t measure_svc_sample_period_ticks(uint32_t reads_per_second)
{
    if (reads_per_second == 0U) {
        return pdMS_TO_TICKS(1000U);
    }

    TickType_t period = pdMS_TO_TICKS(1000U / reads_per_second);
    return period == 0U ? 1U : period;
}

esp_err_t measure_svc_set_sample_rate_hz(uint32_t hz)
{
    s_sample_rate_hz = measure_svc_clamp_sample_rate_hz(hz);
    return ESP_OK;
}

esp_err_t measure_svc_set_average_count(measure_kind_t kind, uint32_t count)
{
    uint32_t *stored_count;
    const char *nvs_key;

    ESP_RETURN_ON_ERROR(
        measure_svc_kind_to_average_slot(kind, &stored_count, &nvs_key),
        TAG,
        "invalid measurement kind");

    count = measure_svc_clamp_average_count(count);
    ESP_RETURN_ON_ERROR(
        measure_svc_store_average_count(nvs_key, count),
        TAG,
        "storing averaging count failed");
    *stored_count = count;
    return ESP_OK;
}

esp_err_t measure_svc_get_average_count(measure_kind_t kind, uint32_t *count)
{
    uint32_t *stored_count;
    const char *nvs_key;

    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_FALSE(s_measure_svc_initialized, ESP_ERR_INVALID_STATE, TAG, "measurement service not initialized");
    ESP_RETURN_ON_ERROR(
        measure_svc_kind_to_average_slot(kind, &stored_count, &nvs_key),
        TAG,
        "invalid measurement kind");
    (void)nvs_key;
    *count = *stored_count;
    return ESP_OK;
}

/**
 * @brief Move a periodic due time forward until it is no longer stale.
 */
static void measure_svc_advance_due_time(TickType_t *due_tick, TickType_t period)
{
    const TickType_t now = xTaskGetTickCount();

    do {
        *due_tick += period;
    } while ((TickType_t)(now - *due_tick) < (TickType_t)(portMAX_DELAY / 2U));
}

/**
 * @brief Read, calibrate, and store one configured physical input sample.
 *
 * The raw provider value is converted before storage, so both single-value reads
 * and DATA queries return ready-to-use samples from the same buffer.
 */
static void measure_svc_sample_input(
    measure_channel_t channel,
    measure_input_t input,
    measure_kind_t kind)
{
    uint32_t raw_value_u4;
    uint32_t stored_value_u4;
    measure_kind_t event_kind;
    esp_err_t err = measure_svc_read_raw(input, MEASURE_KIND_VOLTAGE, &raw_value_u4);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sampling AIN%d failed: %s", (int)input, esp_err_to_name(err));
        return;
    }

    if ((channel == MEASURE_CHANNEL_0) && (kind == MEASURE_KIND_VOLTAGE)) {
        stored_value_u4 = measure_svc_apply_input_voltage_calibration_u4(raw_value_u4);
        event_kind = MEASURE_KIND_VOLTAGE;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        stored_value_u4 = measure_svc_apply_ch0_calibration_u4(raw_value_u4);
        event_kind = MEASURE_KIND_VOLTAGE;
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        stored_value_u4 = measure_svc_apply_current_calibration_u4(raw_value_u4);
        event_kind = MEASURE_KIND_CURRENT;
    } else {
        return;
    }

    const measure_svc_sample_event_t event = {
        .kind = event_kind,
        .channel = channel,
        .physical_input = input,
        .time_ms = measure_svc_time_ms(),
        .raw_value_u4 = raw_value_u4,
        .value_u4 = stored_value_u4,
    };
    measure_svc_publish_sample_event(&event);
}

/**
 * @brief Create synchronization primitives used by the service.
 */
static esp_err_t measure_svc_init_runtime_objects(void)
{
    ESP_RETURN_ON_ERROR(measure_svc_init_listener_registry(), TAG, "initializing listener registry failed");
    return measure_svc_storage_init();
}

static esp_err_t measure_svc_average_latest_samples(
    measure_channel_t channel,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    uint32_t average_count;
    size_t copied_count = 0U;
    uint64_t total_u4 = 0U;

    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(measure_svc_get_average_count(kind, &average_count), TAG, "reading averaging count failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_storage_copy_latest_samples(
            channel,
            kind,
            average_count,
            s_average_samples,
            &copied_count),
        TAG,
        "copying latest samples failed");

    if (copied_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < copied_count; ++i) {
        total_u4 += s_average_samples[i].value_u4;
    }

    *value_u4 = (uint32_t)((total_u4 + (copied_count / 2U)) / copied_count);
    return ESP_OK;
}

static uint32_t measure_svc_calculate_power_u4(uint32_t voltage_u4, uint32_t current_u4)
{
    return (uint32_t)((((uint64_t)voltage_u4 * (uint64_t)current_u4) + 5000ULL) / 10000ULL);
}

static void measure_svc_emit_power_sample(
    const measure_svc_sample_event_t *voltage_event,
    const measure_svc_sample_event_t *current_event,
    const measure_svc_sample_event_t *latest_event)
{
    if ((voltage_event == NULL) || (current_event == NULL) || (latest_event == NULL)) {
        return;
    }

    const measure_svc_sample_event_t power_event = {
        .kind = MEASURE_KIND_POWER,
        .channel = MEASURE_CHANNEL_1,
        .physical_input = s_measure_config.output_current_input,
        .time_ms = latest_event->time_ms,
        .raw_value_u4 = 0U,
        .value_u4 = measure_svc_calculate_power_u4(voltage_event->value_u4, current_event->value_u4),
    };
    measure_svc_publish_sample_event(&power_event);
}

static void measure_svc_derived_power_listener(const measure_svc_sample_event_t *event, void *context)
{
    (void)context;

    if (event == NULL) {
        return;
    }

    if (event->channel != MEASURE_CHANNEL_1) {
        return;
    }

    if (event->kind == MEASURE_KIND_VOLTAGE) {
        s_pending_voltage_power_sample.valid = true;
        s_pending_voltage_power_sample.event = *event;
        if (s_pending_current_power_sample.valid) {
            measure_svc_emit_power_sample(
                &s_pending_voltage_power_sample.event,
                &s_pending_current_power_sample.event,
                event);
            s_pending_voltage_power_sample.valid = false;
            s_pending_current_power_sample.valid = false;
        }
        return;
    }

    if (event->kind == MEASURE_KIND_CURRENT) {
        s_pending_current_power_sample.valid = true;
        s_pending_current_power_sample.event = *event;
        if (s_pending_voltage_power_sample.valid) {
            measure_svc_emit_power_sample(
                &s_pending_voltage_power_sample.event,
                &s_pending_current_power_sample.event,
                event);
            s_pending_voltage_power_sample.valid = false;
            s_pending_current_power_sample.valid = false;
        }
    }
}

/**
 * @brief Start the sampler task once.
 */
esp_err_t measure_svc_start_sampling(void)
{
    ESP_RETURN_ON_FALSE(s_measure_svc_initialized, ESP_ERR_INVALID_STATE, TAG, "measurement service not initialized");

    if (s_sampler_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t task_created = xTaskCreate(
        measure_svc_sampler_task,
        "measure_sampler",
        MEASURE_SVC_SAMPLER_TASK_STACK_WORDS,
        NULL,
        MEASURE_SVC_SAMPLER_TASK_PRIORITY,
        &s_sampler_task_handle);
    if (task_created != pdPASS) {
        s_sampler_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * @brief Background sampler task for configured measurement inputs.
 */
static void measure_svc_sampler_task(void *arg)
{
    (void)arg;

    const uint32_t sample_rate_hz = measure_svc_clamp_sample_rate_hz(s_sample_rate_hz);
    const TickType_t voltage_period = measure_svc_sample_period_ticks(sample_rate_hz);
    const TickType_t current_period = measure_svc_sample_period_ticks(sample_rate_hz);
    TickType_t voltage_due = xTaskGetTickCount();
    TickType_t current_due = voltage_due;
    TickType_t input_voltage_due = voltage_due;

    ESP_LOGI(TAG, "starting sampler at %" PRIu32 " Hz per active input", sample_rate_hz);

    while (true) {
        const TickType_t now = xTaskGetTickCount();

        if ((s_measure_config.input_voltage_input != MEASURE_INPUT_UNUSED) &&
            ((TickType_t)(now - input_voltage_due) < (TickType_t)(portMAX_DELAY / 2U))) {
            measure_svc_sample_input(
                MEASURE_CHANNEL_0,
                s_measure_config.input_voltage_input,
                MEASURE_KIND_VOLTAGE);
            measure_svc_advance_due_time(&input_voltage_due, voltage_period);
        }

        if ((TickType_t)(now - voltage_due) < (TickType_t)(portMAX_DELAY / 2U)) {
            measure_svc_sample_input(
                MEASURE_CHANNEL_1,
                s_measure_config.output_voltage_input,
                MEASURE_KIND_VOLTAGE);
            measure_svc_advance_due_time(&voltage_due, voltage_period);
        }

        if ((TickType_t)(now - current_due) < (TickType_t)(portMAX_DELAY / 2U)) {
            measure_svc_sample_input(
                MEASURE_CHANNEL_1,
                s_measure_config.output_current_input,
                MEASURE_KIND_CURRENT);
            measure_svc_advance_due_time(&current_due, current_period);
        }

        const TickType_t delay_now = xTaskGetTickCount();
        TickType_t voltage_delay =
            ((TickType_t)(delay_now - voltage_due) < (TickType_t)(portMAX_DELAY / 2U)) ? 1U : (voltage_due - delay_now);
        TickType_t current_delay =
            ((TickType_t)(delay_now - current_due) < (TickType_t)(portMAX_DELAY / 2U)) ? 1U : (current_due - delay_now);
        TickType_t input_voltage_delay =
            ((TickType_t)(delay_now - input_voltage_due) < (TickType_t)(portMAX_DELAY / 2U)) ? 1U : (input_voltage_due - delay_now);
        TickType_t min_delay = voltage_delay < current_delay ? voltage_delay : current_delay;
        if (s_measure_config.input_voltage_input != MEASURE_INPUT_UNUSED) {
            min_delay = min_delay < input_voltage_delay ? min_delay : input_voltage_delay;
        }
        vTaskDelay(min_delay);
    }
}

/**
 * @brief Initialize calibration, provider selection, and storage listeners.
 */
esp_err_t measure_svc_init_with_config(const measure_svc_config_t *config)
{
    ESP_RETURN_ON_ERROR(measure_svc_validate_config(config), TAG, "invalid measurement input mapping");

    if (s_measure_svc_initialized) {
        return (s_measure_config.input_voltage_input == config->input_voltage_input) &&
                       (s_measure_config.output_voltage_input == config->output_voltage_input) &&
                       (s_measure_config.output_current_input == config->output_current_input)
                   ? ESP_OK
                   : ESP_ERR_INVALID_STATE;
    }

    s_measure_config = *config;
    ESP_RETURN_ON_ERROR(measure_svc_init_runtime_objects(), TAG, "initializing measurement runtime failed");
    ESP_RETURN_ON_ERROR(measure_svc_calibration_init(), TAG, "loading measurement calibration failed");
    ESP_RETURN_ON_ERROR(measure_svc_average_init(), TAG, "loading measurement averaging failed");
    ESP_RETURN_ON_ERROR(measure_svc_set_prov(measure_prov_ads1115_get()), TAG, "setting measurement provider failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(MEASURE_KIND_VOLTAGE, measure_svc_derived_power_listener, NULL),
        TAG,
        "registering voltage power listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(MEASURE_KIND_CURRENT, measure_svc_derived_power_listener, NULL),
        TAG,
        "registering current power listener failed");
    ESP_RETURN_ON_ERROR(measure_svc_storage_register_listeners(), TAG, "registering storage listeners failed");
    s_measure_svc_initialized = true;
    ESP_LOGI(
        TAG,
        "measurement map: input voltage=%d, output voltage=AIN%d, output current=AIN%d",
        (int)s_measure_config.input_voltage_input,
        (int)s_measure_config.output_voltage_input,
        (int)s_measure_config.output_current_input);
    return ESP_OK;
}

esp_err_t measure_svc_init(void)
{
    const measure_svc_config_t config = {
        .input_voltage_input = MEASURE_INPUT_UNUSED,
        .output_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
        .output_current_input = MEASURE_INPUT_ADS1115_AIN1,
    };
    return measure_svc_init_with_config(&config);
}

/**
 * @brief Set the active measurement provider.
 */
esp_err_t measure_svc_set_prov(const measure_provider_t *provider)
{
    if ((provider == NULL) || (provider->read_value_u4 == NULL) || (provider->read_raw_value_u4 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_prov = provider;
    return ESP_OK;
}

/**
 * @brief Return the latest calibrated logical measurement.
 *
 * This reads the stored ring-buffer value directly; calibration is applied by
 * the sampler before storage.
 */
esp_err_t measure_svc_read(
    measure_channel_t channel,
    measure_kind_t kind,
    uint32_t *value_u4)
{
    if (value_u4 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (channel == MEASURE_CHANNEL_0) {
        ESP_RETURN_ON_FALSE(kind == MEASURE_KIND_VOLTAGE, ESP_ERR_INVALID_ARG, TAG, "invalid CH0 measurement kind");
        ESP_RETURN_ON_FALSE(
            s_measure_config.input_voltage_input != MEASURE_INPUT_UNUSED,
            ESP_ERR_NOT_SUPPORTED,
            TAG,
            "input voltage is not configured");
        return measure_svc_average_latest_samples(channel, kind, value_u4);
    }

    ESP_RETURN_ON_FALSE(channel == MEASURE_CHANNEL_1, ESP_ERR_INVALID_ARG, TAG, "invalid logical measurement channel");
    return measure_svc_average_latest_samples(channel, kind, value_u4);
}

/**
 * @brief Return the active provider name for diagnostics.
 */
const char *measure_svc_get_prov_name(void)
{
    if ((s_active_prov == NULL) || (s_active_prov->name == NULL)) {
        return "UNSET";
    }

    return s_active_prov->name;
}
