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
 * @file wifi_manager.h
 * @brief Persistent WiFi configuration and runtime state holder.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_SSID_MAX_LENGTH 32
#define WIFI_MANAGER_PASSWORD_MAX_LENGTH 64
#define WIFI_MANAGER_IP_MAX_LENGTH 16

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LENGTH + 1];
    char password[WIFI_MANAGER_PASSWORD_MAX_LENGTH + 1];
    bool is_connected;
    uint32_t conn_time_seconds;
    char ip[WIFI_MANAGER_IP_MAX_LENGTH];
} wifi_manager_state_t;

/**
 * @brief Initialize the WiFi manager and load persisted data from NVS.
 *
 * @return
 * - `ESP_OK` if initialization completed successfully
 * - An ESP-IDF error code if NVS initialization or reads fail
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Persist a new SSID value.
 *
 * @param ssid Null-terminated SSID string.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code on validation or storage failure
 */
esp_err_t wifi_manager_set_ssid(const char *ssid);

/**
 * @brief Persist a new WiFi password value.
 *
 * @param password Null-terminated password string.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code on validation or storage failure
 */
esp_err_t wifi_manager_set_password(const char *password);

/**
 * @brief Clear stored SSID and password values.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if erase fails
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Read the current manager state snapshot.
 *
 * @param state Destination structure for the state copy.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if @p state is invalid or the manager is not ready
 */
esp_err_t wifi_manager_get_state(wifi_manager_state_t *state);

/**
 * @brief Check whether a password is currently stored.
 *
 * @return `true` if a non-empty password exists, otherwise `false`
 */
bool wifi_manager_password_is_set(void);

/**
 * @brief Check whether both SSID and password are currently stored.
 *
 * @return `true` if both credential fields are non-empty, otherwise `false`
 */
bool wifi_manager_credentials_are_set(void);

/**
 * @brief Update the runtime connection-related portion of the WiFi state.
 *
 * @param is_connected Current link status.
 * @param ip IPv4 address string to store, or `NULL` to clear it.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the manager is not initialized or @p ip is too long
 */
esp_err_t wifi_manager_set_connection_state(bool is_connected, const char *ip);

#ifdef __cplusplus
}
#endif
