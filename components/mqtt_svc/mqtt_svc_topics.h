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
 * @file mqtt_svc_topics.h
 * @brief Device identity and MQTT topic names.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "mqtt_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_SVC_DEVICE_ID_SIZE 7U
#define MQTT_SVC_TOPIC_MAX_SIZE 96U

typedef struct {
    char device_id[MQTT_SVC_DEVICE_ID_SIZE];
    char client_id[MQTT_SVC_CLIENT_ID_MAX_LENGTH];
    char availability[MQTT_SVC_TOPIC_MAX_SIZE];
    char measurements[MQTT_SVC_TOPIC_MAX_SIZE];
    char relay_state[MQTT_SVC_TOPIC_MAX_SIZE];
    char relay_set[MQTT_SVC_TOPIC_MAX_SIZE];
    char protection_state[MQTT_SVC_TOPIC_MAX_SIZE];
    char discovery[MQTT_SVC_TOPIC_MAX_SIZE];
    char ha_status[MQTT_SVC_TOPIC_MAX_SIZE];
} mqtt_svc_topics_t;

/**
 * @brief Build the device ID, client ID, and all topics.
 *
 * The device ID is the last three bytes of the Wi-Fi STA MAC in lowercase hex.
 */
esp_err_t mqtt_svc_topics_build(
    const uint8_t sta_mac[6],
    const char *discovery_prefix,
    mqtt_svc_topics_t *topics);

#ifdef __cplusplus
}
#endif
