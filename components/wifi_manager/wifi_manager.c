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
 * @file wifi_manager.c
 * @brief Persistent WiFi configuration and runtime state holder.
 */

#include "wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_MANAGER_NVS_NAMESPACE "wifi_cfg"
#define WIFI_MANAGER_NVS_KEY_SSID "ssid"
#define WIFI_MANAGER_NVS_KEY_PASSWORD "password"

static const char *TAG = "wifi_manager";

static struct {
    bool initialized;
    wifi_manager_state_t state;
    int64_t connected_at_us;
} s_wifi_manager;

/**
 * @brief Store a null-terminated string value in the WiFi NVS namespace.
 *
 * @param key NVS key to update.
 * @param value String value to persist.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the namespace cannot be opened or committed
 */
static esp_err_t wifi_manager_store_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

/**
 * @brief Load a string value from the WiFi NVS namespace.
 *
 * Missing keys are treated as empty strings to simplify startup handling.
 *
 * @param key NVS key to read.
 * @param buffer Destination buffer for the loaded string.
 * @param buffer_size Size of @p buffer in bytes.
 *
 * @return
 * - `ESP_OK` if the value was loaded or the key does not exist
 * - An ESP-IDF error code on access failure
 */
static esp_err_t wifi_manager_load_string(const char *key, char *buffer, size_t buffer_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        buffer[0] = '\0';
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    size_t required_size = buffer_size;
    err = nvs_get_str(handle, key, buffer, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        buffer[0] = '\0';
        return ESP_OK;
    }

    return err;
}

/**
 * @brief Remove a stored credential field from the WiFi NVS namespace.
 *
 * @param key NVS key to erase.
 *
 * @return
 * - `ESP_OK` if the key was erased or already absent
 * - An ESP-IDF error code if erase or commit fails
 */
static esp_err_t wifi_manager_erase_key(const char *key)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_erase_key(handle, key);
    if ((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

/**
 * @brief Validate that a credential string is non-NULL and fits the limit.
 *
 * @param value Candidate string to validate.
 * @param max_length Maximum allowed string length excluding the terminator.
 *
 * @return
 * - `ESP_OK` if the value is valid
 * - `ESP_ERR_INVALID_ARG` if @p value is `NULL`
 * - `ESP_ERR_INVALID_SIZE` if the string is too long
 */
static esp_err_t wifi_manager_validate_string_length(const char *value, size_t max_length)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(value) > max_length) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/**
 * @brief Initialize NVS-backed WiFi state from persistent storage.
 *
 * The manager currently owns only configuration and placeholder runtime state.
 * Connectivity is intentionally left for a later implementation pass.
 *
 * @return
 * - `ESP_OK` if the manager is ready
 * - An ESP-IDF error code if NVS initialization or reads fail
 */
esp_err_t wifi_manager_init(void)
{
    if (s_wifi_manager.initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init failed");

    memset(&s_wifi_manager.state, 0, sizeof(s_wifi_manager.state));

    ESP_RETURN_ON_ERROR(
        wifi_manager_load_string(
            WIFI_MANAGER_NVS_KEY_SSID,
            s_wifi_manager.state.ssid,
            sizeof(s_wifi_manager.state.ssid)),
        TAG,
        "loading SSID failed");

    ESP_RETURN_ON_ERROR(
        wifi_manager_load_string(
            WIFI_MANAGER_NVS_KEY_PASSWORD,
            s_wifi_manager.state.password,
            sizeof(s_wifi_manager.state.password)),
        TAG,
        "loading password failed");

    s_wifi_manager.state.is_connected = false;
    s_wifi_manager.state.conn_time_seconds = 0;
    s_wifi_manager.state.ip[0] = '\0';
    s_wifi_manager.connected_at_us = 0;
    s_wifi_manager.initialized = true;

    ESP_LOGI(TAG, "WiFi manager initialized (stored SSID %s)", s_wifi_manager.state.ssid[0] != '\0' ? "present" : "empty");
    return ESP_OK;
}

/**
 * @brief Update the stored WiFi SSID in RAM and NVS.
 *
 * @param ssid New SSID string to store.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the manager is not initialized, validation fails,
 *   or NVS storage fails
 */
esp_err_t wifi_manager_set_ssid(const char *ssid)
{
    ESP_RETURN_ON_FALSE(s_wifi_manager.initialized, ESP_ERR_INVALID_STATE, TAG, "manager not initialized");
    ESP_RETURN_ON_ERROR(
        wifi_manager_validate_string_length(ssid, WIFI_MANAGER_SSID_MAX_LENGTH),
        TAG,
        "invalid SSID");
    ESP_RETURN_ON_ERROR(wifi_manager_store_string(WIFI_MANAGER_NVS_KEY_SSID, ssid), TAG, "storing SSID failed");

    strlcpy(s_wifi_manager.state.ssid, ssid, sizeof(s_wifi_manager.state.ssid));
    return ESP_OK;
}

/**
 * @brief Update the stored WiFi password in RAM and NVS.
 *
 * @param password New password string to store.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the manager is not initialized, validation fails,
 *   or NVS storage fails
 */
esp_err_t wifi_manager_set_password(const char *password)
{
    ESP_RETURN_ON_FALSE(s_wifi_manager.initialized, ESP_ERR_INVALID_STATE, TAG, "manager not initialized");
    ESP_RETURN_ON_ERROR(
        wifi_manager_validate_string_length(password, WIFI_MANAGER_PASSWORD_MAX_LENGTH),
        TAG,
        "invalid password");
    ESP_RETURN_ON_ERROR(
        wifi_manager_store_string(WIFI_MANAGER_NVS_KEY_PASSWORD, password),
        TAG,
        "storing password failed");

    strlcpy(s_wifi_manager.state.password, password, sizeof(s_wifi_manager.state.password));
    return ESP_OK;
}

/**
 * @brief Clear persisted WiFi credentials and reset runtime state placeholders.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the manager is not initialized or NVS erase fails
 */
esp_err_t wifi_manager_clear_credentials(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_manager.initialized, ESP_ERR_INVALID_STATE, TAG, "manager not initialized");
    ESP_RETURN_ON_ERROR(wifi_manager_erase_key(WIFI_MANAGER_NVS_KEY_SSID), TAG, "clearing SSID failed");
    ESP_RETURN_ON_ERROR(wifi_manager_erase_key(WIFI_MANAGER_NVS_KEY_PASSWORD), TAG, "clearing password failed");

    s_wifi_manager.state.ssid[0] = '\0';
    s_wifi_manager.state.password[0] = '\0';
    s_wifi_manager.state.is_connected = false;
    s_wifi_manager.state.conn_time_seconds = 0;
    s_wifi_manager.state.ip[0] = '\0';
    s_wifi_manager.connected_at_us = 0;
    return ESP_OK;
}

/**
 * @brief Retrieve the current WiFi manager state snapshot.
 *
 * @param state Destination structure that receives the state copy.
 *
 * @return
 * - `ESP_OK` on success
 * - An ESP-IDF error code if the manager is not initialized or @p state is `NULL`
 */
esp_err_t wifi_manager_get_state(wifi_manager_state_t *state)
{
    ESP_RETURN_ON_FALSE(s_wifi_manager.initialized, ESP_ERR_INVALID_STATE, TAG, "manager not initialized");
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "state must not be NULL");

    *state = s_wifi_manager.state;
    if (s_wifi_manager.state.is_connected && s_wifi_manager.connected_at_us > 0) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= s_wifi_manager.connected_at_us) {
            state->conn_time_seconds = (uint32_t)((now_us - s_wifi_manager.connected_at_us) / 1000000ULL);
        } else {
            state->conn_time_seconds = 0;
        }
    } else {
        state->conn_time_seconds = 0;
    }

    return ESP_OK;
}

/**
 * @brief Report whether a non-empty password is currently stored.
 *
 * @return `true` when a password is present in RAM, otherwise `false`
 */
bool wifi_manager_password_is_set(void)
{
    return s_wifi_manager.state.password[0] != '\0';
}

bool wifi_manager_credentials_are_set(void)
{
    return (s_wifi_manager.state.ssid[0] != '\0') && (s_wifi_manager.state.password[0] != '\0');
}

esp_err_t wifi_manager_set_connection_state(bool is_connected, const char *ip)
{
    ESP_RETURN_ON_FALSE(s_wifi_manager.initialized, ESP_ERR_INVALID_STATE, TAG, "manager not initialized");

    if (ip == NULL) {
        s_wifi_manager.state.ip[0] = '\0';
    } else {
        ESP_RETURN_ON_ERROR(
            wifi_manager_validate_string_length(ip, WIFI_MANAGER_IP_MAX_LENGTH - 1),
            TAG,
            "invalid IP");
        strlcpy(s_wifi_manager.state.ip, ip, sizeof(s_wifi_manager.state.ip));
    }

    s_wifi_manager.state.is_connected = is_connected;
    s_wifi_manager.state.conn_time_seconds = 0;
    s_wifi_manager.connected_at_us = is_connected ? esp_timer_get_time() : 0;
    return ESP_OK;
}
