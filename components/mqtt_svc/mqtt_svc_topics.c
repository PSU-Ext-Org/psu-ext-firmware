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
 * @file mqtt_svc_topics.c
 * @brief Device identity and MQTT topic names.
 */

#include "mqtt_svc_topics.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool mqtt_svc_topics_format(char *topic, size_t topic_size, const char *format, ...)
{
    va_list args;
    int written;

    va_start(args, format);
    written = vsnprintf(topic, topic_size, format, args);
    va_end(args);
    return (written > 0) && ((size_t)written < topic_size);
}

esp_err_t mqtt_svc_topics_build(
    const uint8_t sta_mac[6],
    const char *discovery_prefix,
    mqtt_svc_topics_t *topics)
{
    bool ok;

    if ((sta_mac == NULL) || (discovery_prefix == NULL) || (topics == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(topics, 0, sizeof(*topics));
    snprintf(
        topics->device_id,
        sizeof(topics->device_id),
        "%02x%02x%02x",
        sta_mac[3],
        sta_mac[4],
        sta_mac[5]);

    const char *id = topics->device_id;
    ok = mqtt_svc_topics_format(topics->client_id, sizeof(topics->client_id), "psu_ext_%s", id);
    ok = ok && mqtt_svc_topics_format(topics->availability, sizeof(topics->availability), "psu_ext/%s/availability", id);
    ok = ok && mqtt_svc_topics_format(topics->measurements, sizeof(topics->measurements), "psu_ext/%s/measurements", id);
    ok = ok && mqtt_svc_topics_format(topics->relay_state, sizeof(topics->relay_state), "psu_ext/%s/relay/state", id);
    ok = ok && mqtt_svc_topics_format(topics->relay_set, sizeof(topics->relay_set), "psu_ext/%s/relay/set", id);
    ok = ok && mqtt_svc_topics_format(
        topics->protection_state,
        sizeof(topics->protection_state),
        "psu_ext/%s/protection/state",
        id);
    ok = ok && mqtt_svc_topics_format(
        topics->discovery,
        sizeof(topics->discovery),
        "%s/device/psu_ext_%s/config",
        discovery_prefix,
        id);
    ok = ok && mqtt_svc_topics_format(topics->ha_status, sizeof(topics->ha_status), "%s/status", discovery_prefix);

    return ok ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
