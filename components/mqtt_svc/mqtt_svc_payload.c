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
 * @file mqtt_svc_payload.c
 * @brief JSON payload builders for telemetry, protection, and discovery.
 */

#include "mqtt_svc_payload.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mqtt_svc_format.h"

typedef enum {
    MQTT_SVC_ENTITY_SENSOR = 0,
    MQTT_SVC_ENTITY_BINARY_SENSOR,
} mqtt_svc_entity_platform_t;

typedef struct {
    const char *key;
    const char *name;
    mqtt_svc_entity_platform_t platform;
    const char *device_class;
    const char *unit;
    bool measurement;
    bool diagnostic;
    int precision;
    bool from_protection;
    bool hidden_by_default;
} mqtt_svc_entity_t;

/*
 * Input OVP is hidden by default: board 1.1.0 does not sample the CH0 input
 * voltage yet, so the pre-enable check never trips. Users can enable it in HA.
 */
static const mqtt_svc_entity_t MQTT_SVC_ENTITIES[] = {
    {"voltage", "Voltage", MQTT_SVC_ENTITY_SENSOR, "voltage", "V", true, false, 3, false, false},
    {"voltage_min", "Voltage min", MQTT_SVC_ENTITY_SENSOR, "voltage", "V", false, false, 3, false, false},
    {"voltage_max", "Voltage max", MQTT_SVC_ENTITY_SENSOR, "voltage", "V", false, false, 3, false, false},
    {"current", "Current", MQTT_SVC_ENTITY_SENSOR, "current", "A", true, false, 3, false, false},
    {"current_max", "Current max", MQTT_SVC_ENTITY_SENSOR, "current", "A", false, false, 3, false, false},
    {"power", "Power", MQTT_SVC_ENTITY_SENSOR, "power", "W", true, false, 2, false, false},
    {"input_ovp", "Input OVP", MQTT_SVC_ENTITY_BINARY_SENSOR, "problem", NULL, false, false, -1, true, true},
    {"output_ovp", "Output OVP", MQTT_SVC_ENTITY_BINARY_SENSOR, "problem", NULL, false, false, -1, true, false},
    {"ocp", "OCP", MQTT_SVC_ENTITY_BINARY_SENSOR, "problem", NULL, false, false, -1, true, false},
    {"cause", "Last change cause", MQTT_SVC_ENTITY_SENSOR, NULL, NULL, false, true, -1, true, false},
    {"ovp_threshold", "OVP threshold", MQTT_SVC_ENTITY_SENSOR, "voltage", "V", false, true, 2, true, false},
    {"ocp_threshold", "OCP threshold", MQTT_SVC_ENTITY_SENSOR, "current", "A", false, true, 3, true, false},
};

const char *mqtt_svc_payload_cause_name(output_ctrl_change_cause_t cause)
{
    switch (cause) {
    case OUTPUT_CTRL_CHANGE_CAUSE_SCPI:
        return "scpi";
    case OUTPUT_CTRL_CHANGE_CAUSE_MQTT:
        return "mqtt";
    case OUTPUT_CTRL_CHANGE_CAUSE_TRIGGER:
        return "trigger";
    case OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OVP:
        return "ovp";
    case OUTPUT_CTRL_CHANGE_CAUSE_PROTECTION_OCP:
        return "ocp";
    case OUTPUT_CTRL_CHANGE_CAUSE_TIMER_EXPIRY:
    case OUTPUT_CTRL_CHANGE_CAUSE_TIMER_CLEAR:
        return "timer";
    default:
        return MQTT_SVC_CAUSE_NONE;
    }
}

bool mqtt_svc_payload_protection_equal(const mqtt_svc_protection_t *a, const mqtt_svc_protection_t *b)
{
    if ((a == NULL) || (b == NULL)) {
        return false;
    }

    return (a->input_ovp == b->input_ovp) &&
           (a->output_ovp == b->output_ovp) &&
           (a->ocp == b->ocp) &&
           (a->ovp_threshold_u4 == b->ovp_threshold_u4) &&
           (a->ocp_threshold_u4 == b->ocp_threshold_u4) &&
           (a->cause != NULL) && (b->cause != NULL) &&
           (strcmp(a->cause, b->cause) == 0);
}

static bool mqtt_svc_payload_add_u4(cJSON *object, const char *key, uint32_t value_u4)
{
    char text[MQTT_SVC_FORMAT_U4_SIZE];
    return mqtt_svc_format_u4(value_u4, text, sizeof(text)) &&
           (cJSON_AddRawToObject(object, key, text) != NULL);
}

static char *mqtt_svc_payload_print(cJSON *root, bool ok)
{
    char *payload = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return payload;
}

char *mqtt_svc_payload_telemetry(const mqtt_svc_telemetry_t *telemetry, bool output_on)
{
    cJSON *root;
    bool ok;

    if (telemetry == NULL) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    ok = mqtt_svc_payload_add_u4(root, "voltage", telemetry->voltage_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "voltage_min", telemetry->voltage_min_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "voltage_max", telemetry->voltage_max_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "current", telemetry->current_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "current_max", telemetry->current_max_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "power", telemetry->power_u4);
    ok = ok && (cJSON_AddStringToObject(root, "output", output_on ? "ON" : "OFF") != NULL);
    return mqtt_svc_payload_print(root, ok);
}

char *mqtt_svc_payload_protection(const mqtt_svc_protection_t *protection)
{
    cJSON *root;
    bool ok;

    if ((protection == NULL) || (protection->cause == NULL)) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    ok = cJSON_AddStringToObject(root, "input_ovp", protection->input_ovp ? "ON" : "OFF") != NULL;
    ok = ok && (cJSON_AddStringToObject(root, "output_ovp", protection->output_ovp ? "ON" : "OFF") != NULL);
    ok = ok && (cJSON_AddStringToObject(root, "ocp", protection->ocp ? "ON" : "OFF") != NULL);
    ok = ok && mqtt_svc_payload_add_u4(root, "ovp_threshold", protection->ovp_threshold_u4);
    ok = ok && mqtt_svc_payload_add_u4(root, "ocp_threshold", protection->ocp_threshold_u4);
    ok = ok && (cJSON_AddStringToObject(root, "cause", protection->cause) != NULL);
    return mqtt_svc_payload_print(root, ok);
}

static bool mqtt_svc_payload_add_entity(
    cJSON *components,
    const mqtt_svc_topics_t *topics,
    const mqtt_svc_entity_t *entity)
{
    char unique_id[48];
    char value_template[48];
    cJSON *component = cJSON_AddObjectToObject(components, entity->key);
    bool ok = component != NULL;

    snprintf(unique_id, sizeof(unique_id), "%s_%s", topics->client_id, entity->key);
    snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", entity->key);

    ok = ok && (cJSON_AddStringToObject(
        component,
        "platform",
        entity->platform == MQTT_SVC_ENTITY_BINARY_SENSOR ? "binary_sensor" : "sensor") != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "name", entity->name) != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "unique_id", unique_id) != NULL);
    ok = ok && (cJSON_AddStringToObject(
        component,
        "state_topic",
        entity->from_protection ? topics->protection_state : topics->measurements) != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "value_template", value_template) != NULL);

    if (ok && (entity->device_class != NULL)) {
        ok = cJSON_AddStringToObject(component, "device_class", entity->device_class) != NULL;
    }
    if (ok && (entity->unit != NULL)) {
        ok = cJSON_AddStringToObject(component, "unit_of_measurement", entity->unit) != NULL;
    }
    if (ok && entity->measurement) {
        ok = cJSON_AddStringToObject(component, "state_class", "measurement") != NULL;
    }
    if (ok && entity->diagnostic) {
        ok = cJSON_AddStringToObject(component, "entity_category", "diagnostic") != NULL;
    }
    if (ok && entity->hidden_by_default) {
        ok = cJSON_AddBoolToObject(component, "enabled_by_default", false) != NULL;
    }
    if (ok && (entity->precision >= 0)) {
        ok = cJSON_AddNumberToObject(component, "suggested_display_precision", entity->precision) != NULL;
    }

    return ok;
}

static bool mqtt_svc_payload_add_output_switch(cJSON *components, const mqtt_svc_topics_t *topics)
{
    char unique_id[48];
    cJSON *component = cJSON_AddObjectToObject(components, "output");
    bool ok = component != NULL;

    snprintf(unique_id, sizeof(unique_id), "%s_output", topics->client_id);
    ok = ok && (cJSON_AddStringToObject(component, "platform", "switch") != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "name", "Output") != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "unique_id", unique_id) != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "command_topic", topics->relay_set) != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "state_topic", topics->relay_state) != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "payload_on", "ON") != NULL);
    ok = ok && (cJSON_AddStringToObject(component, "payload_off", "OFF") != NULL);
    ok = ok && (cJSON_AddBoolToObject(component, "optimistic", false) != NULL);
    ok = ok && (cJSON_AddBoolToObject(component, "retain", false) != NULL);
    return ok;
}

char *mqtt_svc_payload_discovery(const mqtt_svc_topics_t *topics, const char *sw_version)
{
    char device_name[24];
    cJSON *root;
    cJSON *device;
    cJSON *identifiers;
    cJSON *origin;
    cJSON *components;
    bool ok;

    if ((topics == NULL) || (sw_version == NULL)) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    snprintf(device_name, sizeof(device_name), "PSU-EXT %s", topics->device_id);

    device = cJSON_AddObjectToObject(root, "device");
    identifiers = device != NULL ? cJSON_AddArrayToObject(device, "identifiers") : NULL;
    ok = (identifiers != NULL) && cJSON_AddItemToArray(identifiers, cJSON_CreateString(topics->client_id));
    ok = ok && (cJSON_AddStringToObject(device, "name", device_name) != NULL);
    ok = ok && (cJSON_AddStringToObject(device, "manufacturer", "PSU-EXT") != NULL);
    ok = ok && (cJSON_AddStringToObject(device, "model", "PSU-EXT") != NULL);
    ok = ok && (cJSON_AddStringToObject(device, "sw_version", sw_version) != NULL);

    origin = ok ? cJSON_AddObjectToObject(root, "origin") : NULL;
    ok = ok && (origin != NULL);
    ok = ok && (cJSON_AddStringToObject(origin, "name", "psu-ext-firmware") != NULL);
    ok = ok && (cJSON_AddStringToObject(origin, "sw_version", sw_version) != NULL);

    ok = ok && (cJSON_AddStringToObject(root, "availability_topic", topics->availability) != NULL);

    components = ok ? cJSON_AddObjectToObject(root, "components") : NULL;
    ok = ok && (components != NULL);
    for (size_t index = 0U; ok && (index < (sizeof(MQTT_SVC_ENTITIES) / sizeof(MQTT_SVC_ENTITIES[0]))); ++index) {
        ok = mqtt_svc_payload_add_entity(components, topics, &MQTT_SVC_ENTITIES[index]);
    }
    ok = ok && mqtt_svc_payload_add_output_switch(components, topics);

    return mqtt_svc_payload_print(root, ok);
}

void mqtt_svc_payload_free(char *payload)
{
    cJSON_free(payload);
}
