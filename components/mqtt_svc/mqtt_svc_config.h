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
 * @file mqtt_svc_config.h
 * @brief MQTT configuration validation and NVS persistence.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mqtt_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_SVC_CONFIG_KEY_URI "uri"
#define MQTT_SVC_CONFIG_KEY_USER "user"
#define MQTT_SVC_CONFIG_KEY_PASSWORD "pass"
#define MQTT_SVC_CONFIG_KEY_ENABLED "enabled"
#define MQTT_SVC_CONFIG_KEY_INTERVAL "interval_ms"
#define MQTT_SVC_CONFIG_KEY_PREFIX "prefix"

typedef struct {
    char uri[MQTT_SVC_URI_MAX_LENGTH + 1];
    char user[MQTT_SVC_USER_MAX_LENGTH + 1];
    char password[MQTT_SVC_PASSWORD_MAX_LENGTH + 1];
    bool enabled;
    uint32_t interval_ms;
    char prefix[MQTT_SVC_PREFIX_MAX_LENGTH + 1];
} mqtt_svc_config_t;

/** @brief Fill defaults: disabled, empty broker, 1000 ms, `homeassistant`. */
void mqtt_svc_config_set_defaults(mqtt_svc_config_t *config);

/**
 * @brief Validate a broker URI.
 *
 * Accepts `mqtt://` or `mqtts://` followed by a non-empty host. Rejects
 * whitespace, quotes, control characters, and embedded `user:pass@`
 * credentials so the password can only be set through the write-only path.
 */
esp_err_t mqtt_svc_config_validate_uri(const char *uri);

/** @brief Validate a username or password: printable, at most @p max_length. */
esp_err_t mqtt_svc_config_validate_credential(const char *value, size_t max_length);

/** @brief Validate the telemetry interval range. */
esp_err_t mqtt_svc_config_validate_interval(uint32_t interval_ms);

/**
 * @brief Validate a discovery prefix.
 *
 * Allows letters, digits, `_`, `-`, and single `/` separators; rejects MQTT
 * wildcards, leading/trailing `/`, and empty levels.
 */
esp_err_t mqtt_svc_config_validate_prefix(const char *prefix);

/** @brief Load the configuration from NVS, falling back to defaults per key. */
esp_err_t mqtt_svc_config_load(mqtt_svc_config_t *config);

/** @brief Persist one string key in the `mqtt_cfg` namespace. */
esp_err_t mqtt_svc_config_store_string(const char *key, const char *value);

/** @brief Persist one integer key in the `mqtt_cfg` namespace. */
esp_err_t mqtt_svc_config_store_u32(const char *key, uint32_t value);

#ifdef __cplusplus
}
#endif
