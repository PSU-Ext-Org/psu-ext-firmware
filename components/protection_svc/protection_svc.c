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
 * @file protection_svc.c
 * @brief CH1 over-voltage and over-current protection service.
 */

#include "protection_svc.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "measure_svc.h"
#include "nvs.h"
#include "output_ctrl.h"

#define PROTECTION_SVC_NVS_NAMESPACE "prot_cfg"
#define PROTECTION_SVC_NVS_KEY_OVP_U4 "ovp_u4"
#define PROTECTION_SVC_NVS_KEY_CH0_OVP_U4 "ch0_ovp_u4"
#define PROTECTION_SVC_NVS_KEY_OCP_U4 "ocp_u4"
#define PROTECTION_SVC_NVS_KEY_OCP_ENABLED "ocp_en"

static const char *TAG = "protection";

static struct {
    bool initialized;
    uint32_t ch0_ovp_u4;
    uint32_t ch1_ovp_u4;
    uint32_t ocp_u4;
    protection_svc_limits_t limits;
    bool ocp_enabled;
    bool ch0_ovp_tripped;
    bool ch1_ovp_tripped;
    bool ocp_tripped;
    SemaphoreHandle_t lock;
} s_protection;

static bool protection_svc_ovp_channel_is_supported(uint8_t channel)
{
    return (channel == PROTECTION_SVC_CHANNEL_CH0) || (channel == PROTECTION_SVC_CHANNEL_CH1);
}

static bool protection_svc_output_channel_is_supported(uint8_t channel)
{
    return channel == PROTECTION_SVC_CHANNEL_CH1;
}

static const char *protection_svc_ovp_key_for_channel(uint8_t channel)
{
    return channel == PROTECTION_SVC_CHANNEL_CH0 ?
        PROTECTION_SVC_NVS_KEY_CH0_OVP_U4 :
        PROTECTION_SVC_NVS_KEY_OVP_U4;
}

static uint32_t *protection_svc_ovp_value_for_channel(uint8_t channel)
{
    return channel == PROTECTION_SVC_CHANNEL_CH0 ?
        &s_protection.ch0_ovp_u4 :
        &s_protection.ch1_ovp_u4;
}

static bool *protection_svc_ovp_tripped_for_channel(uint8_t channel)
{
    return channel == PROTECTION_SVC_CHANNEL_CH0 ?
        &s_protection.ch0_ovp_tripped :
        &s_protection.ch1_ovp_tripped;
}

static esp_err_t protection_svc_take_lock(void)
{
    ESP_RETURN_ON_FALSE(s_protection.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "lock not initialized");
    return xSemaphoreTake(s_protection.lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t protection_svc_store_u32(const char *key, uint32_t value)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(PROTECTION_SVC_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t protection_svc_load_u32(const char *key, uint32_t default_value, uint32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(value != NULL, ESP_ERR_INVALID_ARG, TAG, "value pointer is null");

    err = nvs_open(PROTECTION_SVC_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_value;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    err = nvs_get_u32(handle, key, value);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_value;
        return ESP_OK;
    }

    return err;
}

static uint32_t protection_svc_safe_default_ovp_u4(void)
{
    if (PROTECTION_SVC_DEFAULT_OVP_U4 <= s_protection.limits.max_ovp_limit_u4) {
        return PROTECTION_SVC_DEFAULT_OVP_U4;
    }

    return s_protection.limits.max_ovp_limit_u4;
}

static void protection_svc_sample_listener(
    const measure_svc_sample_event_t *event,
    void *context);

esp_err_t protection_svc_set_ovp_u4(uint8_t channel, uint32_t threshold_u4)
{
    ESP_RETURN_ON_FALSE(protection_svc_ovp_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported OVP channel");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_FALSE(
        threshold_u4 <= s_protection.limits.max_ovp_limit_u4,
        ESP_ERR_INVALID_ARG,
        TAG,
        "OVP exceeds maximum supported voltage");
    ESP_RETURN_ON_ERROR(protection_svc_store_u32(protection_svc_ovp_key_for_channel(channel), threshold_u4), TAG, "storing OVP failed");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *protection_svc_ovp_value_for_channel(channel) = threshold_u4;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_get_ovp_u4(uint8_t channel, uint32_t *threshold_u4)
{
    ESP_RETURN_ON_FALSE(protection_svc_ovp_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported OVP channel");
    ESP_RETURN_ON_FALSE(threshold_u4 != NULL, ESP_ERR_INVALID_ARG, TAG, "threshold pointer is null");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *threshold_u4 = *protection_svc_ovp_value_for_channel(channel);
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_set_ocp_u4(uint8_t channel, uint32_t threshold_u4)
{
    ESP_RETURN_ON_FALSE(protection_svc_output_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_store_u32(PROTECTION_SVC_NVS_KEY_OCP_U4, threshold_u4), TAG, "storing OCP failed");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    s_protection.ocp_u4 = threshold_u4;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_get_ocp_u4(uint8_t channel, uint32_t *threshold_u4)
{
    ESP_RETURN_ON_FALSE(protection_svc_output_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(threshold_u4 != NULL, ESP_ERR_INVALID_ARG, TAG, "threshold pointer is null");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *threshold_u4 = s_protection.ocp_u4;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_set_ocp_enabled(uint8_t channel, bool enabled)
{
    ESP_RETURN_ON_FALSE(protection_svc_output_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(
        protection_svc_store_u32(PROTECTION_SVC_NVS_KEY_OCP_ENABLED, enabled ? 1U : 0U),
        TAG,
        "storing OCP state failed");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    s_protection.ocp_enabled = enabled;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_get_ocp_enabled(uint8_t channel, bool *enabled)
{
    ESP_RETURN_ON_FALSE(protection_svc_output_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(enabled != NULL, ESP_ERR_INVALID_ARG, TAG, "enabled pointer is null");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *enabled = s_protection.ocp_enabled;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_get_ovp_tripped(uint8_t channel, bool *tripped)
{
    ESP_RETURN_ON_FALSE(protection_svc_ovp_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported OVP channel");
    ESP_RETURN_ON_FALSE(tripped != NULL, ESP_ERR_INVALID_ARG, TAG, "tripped pointer is null");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *tripped = *protection_svc_ovp_tripped_for_channel(channel);
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_get_ocp_tripped(uint8_t channel, bool *tripped)
{
    ESP_RETURN_ON_FALSE(protection_svc_output_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(tripped != NULL, ESP_ERR_INVALID_ARG, TAG, "tripped pointer is null");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    *tripped = s_protection.ocp_tripped;
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_clear_trips(uint8_t channel)
{
    ESP_RETURN_ON_FALSE(protection_svc_ovp_channel_is_supported(channel), ESP_ERR_INVALID_ARG, TAG, "unsupported channel");
    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    if (channel == PROTECTION_SVC_CHANNEL_CH0) {
        s_protection.ch0_ovp_tripped = false;
    } else {
        s_protection.ch1_ovp_tripped = false;
        s_protection.ocp_tripped = false;
    }
    xSemaphoreGive(s_protection.lock);
    return ESP_OK;
}

esp_err_t protection_svc_check_ch1_enable_allowed(void)
{
    uint32_t input_voltage_u4;
    uint32_t threshold_u4;
    bool should_trip;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_protection.initialized, ESP_ERR_INVALID_STATE, TAG, "service not initialized");
    err = measure_svc_read(MEASURE_CHANNEL_0, MEASURE_KIND_VOLTAGE, &input_voltage_u4);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "reading CH0 input voltage failed");

    ESP_RETURN_ON_ERROR(protection_svc_take_lock(), TAG, "taking lock failed");
    threshold_u4 = s_protection.ch0_ovp_u4;
    should_trip = input_voltage_u4 > threshold_u4;
    if (should_trip) {
        s_protection.ch0_ovp_tripped = true;
    }
    xSemaphoreGive(s_protection.lock);

    if (should_trip) {
        ESP_LOGW(
            TAG,
            "CH0 OVP blocks relay enable at %lu.%04lu V threshold %lu.%04lu V",
            (unsigned long)(input_voltage_u4 / 10000U),
            (unsigned long)(input_voltage_u4 % 10000U),
            (unsigned long)(threshold_u4 / 10000U),
            (unsigned long)(threshold_u4 % 10000U));
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static void protection_svc_check_sample(
    measure_channel_t channel,
    measure_kind_t kind,
    uint32_t value_u4)
{
    bool should_trip = false;
    bool was_already_tripped = false;
    esp_err_t output_err = ESP_OK;

    if (!s_protection.initialized || (s_protection.lock == NULL)) {
        return;
    }

    if (protection_svc_take_lock() != ESP_OK) {
        return;
    }

    if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_VOLTAGE)) {
        should_trip = value_u4 > s_protection.ch1_ovp_u4;
        was_already_tripped = s_protection.ch1_ovp_tripped;
        if (should_trip) {
            s_protection.ch1_ovp_tripped = true;
        }
    } else if ((channel == MEASURE_CHANNEL_1) && (kind == MEASURE_KIND_CURRENT)) {
        should_trip = (value_u4 > s_protection.limits.hard_current_limit_u4) ||
                      (s_protection.ocp_enabled && (value_u4 > s_protection.ocp_u4));
        was_already_tripped = s_protection.ocp_tripped;
        if (should_trip) {
            s_protection.ocp_tripped = true;
        }
    }

    if (should_trip) {
        output_err = output_ctrl_set_with_cause(
            PROTECTION_SVC_CHANNEL_CH1,
            false,
            kind == MEASURE_KIND_VOLTAGE ?
                OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OVP :
                OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP);
    }

    xSemaphoreGive(s_protection.lock);

    if (should_trip && !was_already_tripped) {
        ESP_LOGW(
            TAG,
            "%s trip at %lu.%04lu, relay disconnect %s",
            kind == MEASURE_KIND_VOLTAGE ? "OVP" : "OCP",
            (unsigned long)(value_u4 / 10000U),
            (unsigned long)(value_u4 % 10000U),
            output_err == ESP_OK ? "OK" : esp_err_to_name(output_err));
    }
}

static void protection_svc_sample_listener(
    const measure_svc_sample_event_t *event,
    void *context)
{
    (void)context;

    if (event == NULL) {
        return;
    }

    protection_svc_check_sample(event->channel, event->kind, event->value_u4);
}

esp_err_t protection_svc_init_with_limits(const protection_svc_limits_t *limits)
{
    ESP_RETURN_ON_FALSE(limits != NULL, ESP_ERR_INVALID_ARG, TAG, "limits pointer is null");
    ESP_RETURN_ON_FALSE(limits->hard_current_limit_u4 > 0U, ESP_ERR_INVALID_ARG, TAG, "hard current limit must be > 0");
    ESP_RETURN_ON_FALSE(limits->max_ovp_limit_u4 > 0U, ESP_ERR_INVALID_ARG, TAG, "max OVP limit must be > 0");

    if (s_protection.initialized) {
        return ESP_OK;
    }

    if (s_protection.lock == NULL) {
        s_protection.lock = xSemaphoreCreateMutex();
        if (s_protection.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_protection.limits = *limits;

    ESP_RETURN_ON_ERROR(
        protection_svc_load_u32(
            PROTECTION_SVC_NVS_KEY_CH0_OVP_U4,
            protection_svc_safe_default_ovp_u4(),
            &s_protection.ch0_ovp_u4),
        TAG,
        "loading CH0 OVP failed");
    if (s_protection.ch0_ovp_u4 > s_protection.limits.max_ovp_limit_u4) {
        s_protection.ch0_ovp_u4 = protection_svc_safe_default_ovp_u4();
        ESP_RETURN_ON_ERROR(
            protection_svc_store_u32(PROTECTION_SVC_NVS_KEY_CH0_OVP_U4, s_protection.ch0_ovp_u4),
            TAG,
            "correcting stored CH0 OVP failed");
    }

    ESP_RETURN_ON_ERROR(
        protection_svc_load_u32(
            PROTECTION_SVC_NVS_KEY_OVP_U4,
            protection_svc_safe_default_ovp_u4(),
            &s_protection.ch1_ovp_u4),
        TAG,
        "loading CH1 OVP failed");
    if (s_protection.ch1_ovp_u4 > s_protection.limits.max_ovp_limit_u4) {
        s_protection.ch1_ovp_u4 = protection_svc_safe_default_ovp_u4();
        ESP_RETURN_ON_ERROR(
            protection_svc_store_u32(PROTECTION_SVC_NVS_KEY_OVP_U4, s_protection.ch1_ovp_u4),
            TAG,
            "correcting stored CH1 OVP failed");
    }
    ESP_RETURN_ON_ERROR(
        protection_svc_load_u32(
            PROTECTION_SVC_NVS_KEY_OCP_U4,
            PROTECTION_SVC_DEFAULT_OCP_U4,
            &s_protection.ocp_u4),
        TAG,
        "loading OCP failed");

    uint32_t ocp_enabled = 1U;
    ESP_RETURN_ON_ERROR(
        protection_svc_load_u32(PROTECTION_SVC_NVS_KEY_OCP_ENABLED, 1U, &ocp_enabled),
        TAG,
        "loading OCP state failed");
    s_protection.ocp_enabled = ocp_enabled != 0U;
    s_protection.ch0_ovp_tripped = false;
    s_protection.ch1_ovp_tripped = false;
    s_protection.ocp_tripped = false;
    s_protection.initialized = true;

    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(
            MEASURE_KIND_VOLTAGE,
            protection_svc_sample_listener,
            NULL),
        TAG,
        "registering voltage protection listener failed");
    ESP_RETURN_ON_ERROR(
        measure_svc_register_sample_listener(
            MEASURE_KIND_CURRENT,
            protection_svc_sample_listener,
            NULL),
        TAG,
        "registering current protection listener failed");

    ESP_LOGI(
        TAG,
        "Protection initialized CH0 OVP=%lu.%04lu V CH1 OVP=%lu.%04lu V OCP=%lu.%04lu A OCP state=%u hard current=%lu.%04lu A max OVP=%lu.%04lu V",
        (unsigned long)(s_protection.ch0_ovp_u4 / 10000U),
        (unsigned long)(s_protection.ch0_ovp_u4 % 10000U),
        (unsigned long)(s_protection.ch1_ovp_u4 / 10000U),
        (unsigned long)(s_protection.ch1_ovp_u4 % 10000U),
        (unsigned long)(s_protection.ocp_u4 / 10000U),
        (unsigned long)(s_protection.ocp_u4 % 10000U),
        s_protection.ocp_enabled ? 1U : 0U,
        (unsigned long)(s_protection.limits.hard_current_limit_u4 / 10000U),
        (unsigned long)(s_protection.limits.hard_current_limit_u4 % 10000U),
        (unsigned long)(s_protection.limits.max_ovp_limit_u4 / 10000U),
        (unsigned long)(s_protection.limits.max_ovp_limit_u4 % 10000U));
    return ESP_OK;
}
