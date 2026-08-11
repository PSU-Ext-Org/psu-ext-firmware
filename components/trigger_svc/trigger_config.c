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
 * @file trigger_config.c
 * @brief Persistent trigger mapping configuration service.
 */

#include "trigger_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "trigger_action_builtin.h"

#define TRIGGER_CONFIG_NVS_NAMESPACE "trig_cfg"

static const char *TAG = "trigger_cfg";

typedef struct {
    bool initialized;
    trigger_config_mapping_t current[2U][2U];
} trigger_config_runtime_t;

typedef struct {
    const char *function_key;
    const char *arguments_key;
} trigger_config_slot_keys_t;

static const trigger_config_slot_keys_t s_slot_keys[2U][2U] = {
    {
        { .function_key = "t1l_func", .arguments_key = "t1l_args" },
        { .function_key = "t1h_func", .arguments_key = "t1h_args" },
    },
    {
        { .function_key = "t2l_func", .arguments_key = "t2l_args" },
        { .function_key = "t2h_func", .arguments_key = "t2h_args" },
    },
};

static trigger_config_runtime_t s_trigger_config;

static bool trigger_config_state_is_valid(trigger_svc_state_t state)
{
    return (state == TRIGGER_SVC_STATE_LOW) || (state == TRIGGER_SVC_STATE_HIGH);
}

static size_t trigger_config_state_to_index(trigger_svc_state_t state)
{
    return state == TRIGGER_SVC_STATE_LOW ? 0U : 1U;
}

static esp_err_t trigger_config_mapping_to_indices(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    size_t *trigger_index,
    size_t *state_index)
{
    ESP_RETURN_ON_FALSE(trigger_index != NULL, ESP_ERR_INVALID_ARG, TAG, "trigger index pointer is null");
    ESP_RETURN_ON_FALSE(state_index != NULL, ESP_ERR_INVALID_ARG, TAG, "state index pointer is null");
    ESP_RETURN_ON_FALSE((trigger_id == 1U) || (trigger_id == 2U), ESP_ERR_INVALID_ARG, TAG, "unsupported trigger id");
    ESP_RETURN_ON_FALSE(trigger_config_state_is_valid(state), ESP_ERR_INVALID_ARG, TAG, "unsupported trigger state");

    *trigger_index = (size_t)(trigger_id - 1U);
    *state_index = trigger_config_state_to_index(state);
    return ESP_OK;
}

static void trigger_config_set_default_mapping(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    trigger_config_mapping_t *mapping)
{
    if (mapping == NULL) {
        return;
    }

    memset(mapping, 0, sizeof(*mapping));
    mapping->trigger_id = trigger_id;
    mapping->state = state;

    if ((trigger_id == 1U) && (state == TRIGGER_SVC_STATE_LOW)) {
        strlcpy(mapping->function_name, "OUT_ON", sizeof(mapping->function_name));
        strlcpy(mapping->arguments, "CH1", sizeof(mapping->arguments));
    } else if ((trigger_id == 2U) && (state == TRIGGER_SVC_STATE_LOW)) {
        strlcpy(mapping->function_name, "OUT_OFF", sizeof(mapping->function_name));
        strlcpy(mapping->arguments, "CH1", sizeof(mapping->arguments));
    }
}

static esp_err_t trigger_config_resolve_mapping(
    const trigger_config_mapping_t *mapping,
    const trigger_action_t **action,
    trigger_config_mapping_t *normalized_mapping)
{
    trigger_config_mapping_t local_mapping;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping is null");
    ESP_RETURN_ON_FALSE(action != NULL, ESP_ERR_INVALID_ARG, TAG, "action pointer is null");

    local_mapping = *mapping;
    if (normalized_mapping == NULL) {
        normalized_mapping = &local_mapping;
    } else {
        *normalized_mapping = *mapping;
    }

    if (normalized_mapping->function_name[0] == '\0') {
        normalized_mapping->arguments[0] = '\0';
        *action = NULL;
        return ESP_OK;
    }

    err = trigger_action_builtin_resolve(
        normalized_mapping->function_name,
        normalized_mapping->arguments,
        action,
        normalized_mapping->function_name,
        sizeof(normalized_mapping->function_name),
        normalized_mapping->arguments,
        sizeof(normalized_mapping->arguments));
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t trigger_config_store_mapping(const trigger_config_mapping_t *mapping)
{
    nvs_handle_t handle;
    size_t trigger_index;
    size_t state_index;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping is null");
    ESP_RETURN_ON_ERROR(
        trigger_config_mapping_to_indices(mapping->trigger_id, mapping->state, &trigger_index, &state_index),
        TAG,
        "invalid trigger mapping indices");

    ESP_RETURN_ON_ERROR(nvs_open(TRIGGER_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    if (mapping->function_name[0] == '\0') {
        err = nvs_erase_key(handle, s_slot_keys[trigger_index][state_index].function_key);
        if ((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) {
            err = nvs_erase_key(handle, s_slot_keys[trigger_index][state_index].arguments_key);
        }
        if ((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    err = nvs_set_str(handle, s_slot_keys[trigger_index][state_index].function_key, mapping->function_name);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, s_slot_keys[trigger_index][state_index].arguments_key, mapping->arguments);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t trigger_config_load_mapping(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    trigger_config_mapping_t *mapping)
{
    nvs_handle_t handle;
    size_t required_size;
    size_t trigger_index;
    size_t state_index;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping pointer is null");
    trigger_config_set_default_mapping(trigger_id, state, mapping);
    ESP_RETURN_ON_ERROR(
        trigger_config_mapping_to_indices(trigger_id, state, &trigger_index, &state_index),
        TAG,
        "invalid trigger mapping indices");

    err = nvs_open(TRIGGER_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    required_size = sizeof(mapping->function_name);
    err = nvs_get_str(
        handle,
        s_slot_keys[trigger_index][state_index].function_key,
        mapping->function_name,
        &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    required_size = sizeof(mapping->arguments);
    err = nvs_get_str(
        handle,
        s_slot_keys[trigger_index][state_index].arguments_key,
        mapping->arguments,
        &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        mapping->arguments[0] = '\0';
        nvs_close(handle);
        return ESP_OK;
    }

    nvs_close(handle);
    return err;
}

static esp_err_t trigger_config_apply_mapping(const trigger_config_mapping_t *mapping)
{
    trigger_config_mapping_t normalized_mapping;
    const trigger_action_t *action;
    trigger_svc_pin_t pin;
    size_t trigger_index;
    size_t state_index;

    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping is null");
    ESP_RETURN_ON_ERROR(
        trigger_config_resolve_mapping(mapping, &action, &normalized_mapping),
        TAG,
        "resolving trigger mapping failed");
    ESP_RETURN_ON_ERROR(trigger_svc_trigger_id_to_pin(normalized_mapping.trigger_id, &pin), TAG, "invalid trigger id");
    ESP_RETURN_ON_ERROR(
        trigger_config_mapping_to_indices(
            normalized_mapping.trigger_id,
            normalized_mapping.state,
            &trigger_index,
            &state_index),
        TAG,
        "invalid trigger mapping indices");
    ESP_RETURN_ON_ERROR(
        trigger_svc_set_action(pin, normalized_mapping.state, action),
        TAG,
        "binding trigger mapping failed");

    s_trigger_config.current[trigger_index][state_index] = normalized_mapping;
    return ESP_OK;
}

esp_err_t trigger_config_init(void)
{
    if (s_trigger_config.initialized) {
        return ESP_OK;
    }

    for (uint8_t trigger_id = 1U; trigger_id <= 2U; ++trigger_id) {
        for (size_t state_index = 0U; state_index < 2U; ++state_index) {
            const trigger_svc_state_t state =
                (state_index == 0U) ? TRIGGER_SVC_STATE_LOW : TRIGGER_SVC_STATE_HIGH;
            trigger_config_mapping_t loaded_mapping;
            esp_err_t err = trigger_config_load_mapping(trigger_id, state, &loaded_mapping);
            if (err != ESP_OK) {
                return err;
            }

            err = trigger_config_apply_mapping(&loaded_mapping);
            if (err == ESP_OK) {
                continue;
            }

            ESP_LOGW(
                TAG,
                "ignoring invalid stored mapping for trigger %u state=%s: %s",
                trigger_id,
                state == TRIGGER_SVC_STATE_LOW ? "LOW" : "HIGH",
                esp_err_to_name(err));

            trigger_config_set_default_mapping(trigger_id, state, &loaded_mapping);
            ESP_RETURN_ON_ERROR(trigger_config_apply_mapping(&loaded_mapping), TAG, "applying default mapping failed");
        }
    }

    s_trigger_config.initialized = true;
    return ESP_OK;
}

esp_err_t trigger_config_bind(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    const char *function_name,
    const char *arguments)
{
    trigger_config_mapping_t new_mapping;
    trigger_config_mapping_t old_mapping;
    const trigger_action_t *old_action;
    const trigger_action_t *new_action;
    trigger_svc_pin_t pin;
    size_t trigger_index;
    size_t state_index;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_trigger_config.initialized, ESP_ERR_INVALID_STATE, TAG, "trigger config not initialized");
    ESP_RETURN_ON_FALSE(function_name != NULL, ESP_ERR_INVALID_ARG, TAG, "function name is null");
    ESP_RETURN_ON_FALSE(arguments != NULL, ESP_ERR_INVALID_ARG, TAG, "arguments are null");
    ESP_RETURN_ON_ERROR(trigger_svc_trigger_id_to_pin(trigger_id, &pin), TAG, "invalid trigger id");
    ESP_RETURN_ON_ERROR(
        trigger_config_mapping_to_indices(trigger_id, state, &trigger_index, &state_index),
        TAG,
        "invalid trigger mapping indices");

    memset(&new_mapping, 0, sizeof(new_mapping));
    new_mapping.trigger_id = trigger_id;
    new_mapping.state = state;
    strlcpy(new_mapping.function_name, function_name, sizeof(new_mapping.function_name));
    strlcpy(new_mapping.arguments, arguments, sizeof(new_mapping.arguments));

    ESP_RETURN_ON_ERROR(
        trigger_config_resolve_mapping(&new_mapping, &new_action, &new_mapping),
        TAG,
        "invalid trigger function or arguments");

    old_mapping = s_trigger_config.current[trigger_index][state_index];
    ESP_RETURN_ON_ERROR(
        trigger_svc_get_action(pin, state, &old_action),
        TAG,
        "reading existing trigger action failed");
    ESP_RETURN_ON_ERROR(trigger_svc_set_action(pin, state, new_action), TAG, "binding trigger action failed");

    err = trigger_config_store_mapping(&new_mapping);
    if (err != ESP_OK) {
        const esp_err_t rollback_err = trigger_svc_set_action(pin, state, old_action);
        if (rollback_err != ESP_OK) {
            ESP_LOGE(TAG, "rollback failed for trigger %u state=%u: %s", trigger_id, (unsigned)state, esp_err_to_name(rollback_err));
        }
        return err;
    }

    s_trigger_config.current[trigger_index][state_index] = new_mapping;
    return ESP_OK;
}

esp_err_t trigger_config_get(
    uint8_t trigger_id,
    trigger_svc_state_t state,
    trigger_config_mapping_t *mapping)
{
    size_t trigger_index;
    size_t state_index;

    ESP_RETURN_ON_FALSE(s_trigger_config.initialized, ESP_ERR_INVALID_STATE, TAG, "trigger config not initialized");
    ESP_RETURN_ON_FALSE(mapping != NULL, ESP_ERR_INVALID_ARG, TAG, "mapping pointer is null");
    ESP_RETURN_ON_ERROR(
        trigger_config_mapping_to_indices(trigger_id, state, &trigger_index, &state_index),
        TAG,
        "invalid trigger mapping indices");

    *mapping = s_trigger_config.current[trigger_index][state_index];
    return ESP_OK;
}
