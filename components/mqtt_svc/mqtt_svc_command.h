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
 * @file mqtt_svc_command.h
 * @brief Filtering of inbound MQTT messages.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MQTT_SVC_COMMAND_IGNORE = 0,
    MQTT_SVC_COMMAND_RETAINED,
    MQTT_SVC_COMMAND_ON,
    MQTT_SVC_COMMAND_OFF,
} mqtt_svc_command_t;

/**
 * @brief Classify a `relay/set` message.
 *
 * Retained messages are reported as `MQTT_SVC_COMMAND_RETAINED` so they can
 * be logged and dropped. Only the exact payloads `ON` and `OFF` are accepted.
 */
mqtt_svc_command_t mqtt_svc_command_parse_relay(const char *data, size_t length, bool retained);

/** @brief Report whether a Home Assistant status payload is exactly `online`. */
bool mqtt_svc_command_is_ha_online(const char *data, size_t length);

#ifdef __cplusplus
}
#endif
