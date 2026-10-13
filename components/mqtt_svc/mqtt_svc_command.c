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
 * @file mqtt_svc_command.c
 * @brief Filtering of inbound MQTT messages.
 */

#include "mqtt_svc_command.h"

#include <string.h>

static bool mqtt_svc_command_payload_equals(const char *data, size_t length, const char *expected)
{
    const size_t expected_length = strlen(expected);
    return (data != NULL) && (length == expected_length) && (memcmp(data, expected, length) == 0);
}

mqtt_svc_command_t mqtt_svc_command_parse_relay(const char *data, size_t length, bool retained)
{
    if (retained) {
        return MQTT_SVC_COMMAND_RETAINED;
    }

    if (mqtt_svc_command_payload_equals(data, length, "ON")) {
        return MQTT_SVC_COMMAND_ON;
    }

    if (mqtt_svc_command_payload_equals(data, length, "OFF")) {
        return MQTT_SVC_COMMAND_OFF;
    }

    return MQTT_SVC_COMMAND_IGNORE;
}

bool mqtt_svc_command_is_ha_online(const char *data, size_t length)
{
    return mqtt_svc_command_payload_equals(data, length, "online");
}
