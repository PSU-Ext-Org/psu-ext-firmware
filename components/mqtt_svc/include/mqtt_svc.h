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
 * @file mqtt_svc.h
 * @brief MQTT client publishing PSU-EXT to Home Assistant via MQTT discovery.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_SVC_URI_MAX_LENGTH 96
#define MQTT_SVC_USER_MAX_LENGTH 64
#define MQTT_SVC_PASSWORD_MAX_LENGTH 64
#define MQTT_SVC_PREFIX_MAX_LENGTH 32
#define MQTT_SVC_CLIENT_ID_MAX_LENGTH 16
#define MQTT_SVC_ERROR_MAX_LENGTH 48

#define MQTT_SVC_DEFAULT_INTERVAL_MS 1000U
#define MQTT_SVC_MIN_INTERVAL_MS 200U
#define MQTT_SVC_MAX_INTERVAL_MS 3600000U
#define MQTT_SVC_DEFAULT_PREFIX "homeassistant"

typedef enum {
    MQTT_SVC_STATE_DISABLED = 0,
    MQTT_SVC_STATE_WAIT_WIFI,
    MQTT_SVC_STATE_CONNECTING,
    MQTT_SVC_STATE_CONNECTED,
    MQTT_SVC_STATE_ERROR,
} mqtt_svc_state_t;

typedef struct {
    mqtt_svc_state_t state;
    char client_id[MQTT_SVC_CLIENT_ID_MAX_LENGTH];
    char last_error[MQTT_SVC_ERROR_MAX_LENGTH];
} mqtt_svc_status_t;

/**
 * @brief Load persisted MQTT configuration and start the connection manager.
 *
 * Requires NVS, the default event loop, `output_ctrl`, `measure_svc`, and
 * `protection_svc` to be initialized. The client connects only when enabled
 * and Wi-Fi has an IP address.
 */
esp_err_t mqtt_svc_init(void);

/**
 * @brief Persist the broker URI. The scheme must be `mqtt://` or `mqtts://`.
 *
 * Applies immediately by reconnecting when the client is enabled.
 */
esp_err_t mqtt_svc_set_uri(const char *uri);
esp_err_t mqtt_svc_get_uri(char *uri, size_t uri_size);

/** @brief Persist the broker username; an empty string disables auth. */
esp_err_t mqtt_svc_set_user(const char *user);
esp_err_t mqtt_svc_get_user(char *user, size_t user_size);

/** @brief Persist the broker password. There is intentionally no getter. */
esp_err_t mqtt_svc_set_password(const char *password);
bool mqtt_svc_password_is_set(void);

/** @brief Persist and apply the client enable flag. */
esp_err_t mqtt_svc_set_enabled(bool enabled);
esp_err_t mqtt_svc_get_enabled(bool *enabled);

/** @brief Persist the telemetry interval in milliseconds. */
esp_err_t mqtt_svc_set_interval_ms(uint32_t interval_ms);
esp_err_t mqtt_svc_get_interval_ms(uint32_t *interval_ms);

/** @brief Persist the Home Assistant discovery prefix. */
esp_err_t mqtt_svc_set_prefix(const char *prefix);
esp_err_t mqtt_svc_get_prefix(char *prefix, size_t prefix_size);

/** @brief Read the connection state, client ID, and last error text. */
esp_err_t mqtt_svc_get_status(mqtt_svc_status_t *status);

/** @brief Uppercase token used by `SYST:MQTT:STAT?` for a state. */
const char *mqtt_svc_state_name(mqtt_svc_state_t state);

#ifdef __cplusplus
}
#endif
