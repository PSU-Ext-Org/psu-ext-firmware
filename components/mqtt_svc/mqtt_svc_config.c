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
 * @file mqtt_svc_config.c
 * @brief MQTT configuration validation and NVS persistence.
 */

#include "mqtt_svc_config.h"

#include <string.h>

#include "esp_check.h"
#include "nvs.h"

#define MQTT_SVC_CONFIG_NVS_NAMESPACE "mqtt_cfg"

static const char *TAG = "mqtt_cfg";

static bool mqtt_svc_config_char_is_printable(char value)
{
    return (value > ' ') && (value < 0x7f);
}

void mqtt_svc_config_set_defaults(mqtt_svc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enabled = false;
    config->interval_ms = MQTT_SVC_DEFAULT_INTERVAL_MS;
    strlcpy(config->prefix, MQTT_SVC_DEFAULT_PREFIX, sizeof(config->prefix));
}

esp_err_t mqtt_svc_config_validate_uri(const char *uri)
{
    const char *host;

    if (uri == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(uri) > MQTT_SVC_URI_MAX_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (strncmp(uri, "mqtt://", 7) == 0) {
        host = uri + 7;
    } else if (strncmp(uri, "mqtts://", 8) == 0) {
        host = uri + 8;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if ((*host == '\0') || (*host == '/') || (*host == ':')) {
        return ESP_ERR_INVALID_ARG;
    }

    for (const char *cursor = uri; *cursor != '\0'; ++cursor) {
        if (!mqtt_svc_config_char_is_printable(*cursor) || (*cursor == '"') || (*cursor == '@')) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return ESP_OK;
}

esp_err_t mqtt_svc_config_validate_credential(const char *value, size_t max_length)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(value) > max_length) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if ((*cursor != ' ') && !mqtt_svc_config_char_is_printable(*cursor)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return ESP_OK;
}

esp_err_t mqtt_svc_config_validate_interval(uint32_t interval_ms)
{
    if ((interval_ms < MQTT_SVC_MIN_INTERVAL_MS) || (interval_ms > MQTT_SVC_MAX_INTERVAL_MS)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t mqtt_svc_config_validate_prefix(const char *prefix)
{
    size_t length;

    if (prefix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    length = strlen(prefix);
    if (length > MQTT_SVC_PREFIX_MAX_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    if ((length == 0U) || (prefix[0] == '/') || (prefix[length - 1U] == '/')) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0U; index < length; ++index) {
        const char value = prefix[index];
        const bool is_alnum = ((value >= 'a') && (value <= 'z')) ||
                              ((value >= 'A') && (value <= 'Z')) ||
                              ((value >= '0') && (value <= '9'));

        if (value == '/') {
            if (prefix[index + 1U] == '/') {
                return ESP_ERR_INVALID_ARG;
            }
        } else if (!is_alnum && (value != '_') && (value != '-')) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return ESP_OK;
}

static esp_err_t mqtt_svc_config_load_string(
    nvs_handle_t handle,
    const char *key,
    char *buffer,
    size_t buffer_size)
{
    size_t required_size = buffer_size;
    const esp_err_t err = nvs_get_str(handle, key, buffer, &required_size);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t mqtt_svc_config_load(mqtt_svc_config_t *config)
{
    nvs_handle_t handle;
    uint32_t enabled = 0U;
    uint32_t interval_ms = MQTT_SVC_DEFAULT_INTERVAL_MS;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config pointer is null");
    mqtt_svc_config_set_defaults(config);

    err = nvs_open(MQTT_SVC_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    err = mqtt_svc_config_load_string(handle, MQTT_SVC_CONFIG_KEY_URI, config->uri, sizeof(config->uri));
    if (err == ESP_OK) {
        err = mqtt_svc_config_load_string(handle, MQTT_SVC_CONFIG_KEY_USER, config->user, sizeof(config->user));
    }
    if (err == ESP_OK) {
        err = mqtt_svc_config_load_string(
            handle,
            MQTT_SVC_CONFIG_KEY_PASSWORD,
            config->password,
            sizeof(config->password));
    }
    if (err == ESP_OK) {
        err = mqtt_svc_config_load_string(handle, MQTT_SVC_CONFIG_KEY_PREFIX, config->prefix, sizeof(config->prefix));
    }
    if (err == ESP_OK) {
        err = nvs_get_u32(handle, MQTT_SVC_CONFIG_KEY_ENABLED, &enabled);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    if (err == ESP_OK) {
        err = nvs_get_u32(handle, MQTT_SVC_CONFIG_KEY_INTERVAL, &interval_ms);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "loading MQTT config failed");

    config->enabled = enabled != 0U;
    config->interval_ms = mqtt_svc_config_validate_interval(interval_ms) == ESP_OK ?
        interval_ms :
        MQTT_SVC_DEFAULT_INTERVAL_MS;
    if (mqtt_svc_config_validate_prefix(config->prefix) != ESP_OK) {
        strlcpy(config->prefix, MQTT_SVC_DEFAULT_PREFIX, sizeof(config->prefix));
    }
    if ((config->uri[0] != '\0') && (mqtt_svc_config_validate_uri(config->uri) != ESP_OK)) {
        config->uri[0] = '\0';
    }

    return ESP_OK;
}

esp_err_t mqtt_svc_config_store_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(MQTT_SVC_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t mqtt_svc_config_store_u32(const char *key, uint32_t value)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(MQTT_SVC_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
