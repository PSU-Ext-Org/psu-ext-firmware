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
 * @file mqtt_svc_payload.h
 * @brief JSON payload builders for telemetry, protection, and discovery.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mqtt_svc_topics.h"
#include "mqtt_svc_window.h"
#include "output_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_SVC_CAUSE_NONE "none"

typedef struct {
    bool input_ovp;
    bool output_ovp;
    bool ocp;
    uint32_t ovp_threshold_u4;
    uint32_t ocp_threshold_u4;
    const char *cause;
} mqtt_svc_protection_t;

/** @brief Lowercase name reported for an output change cause. */
const char *mqtt_svc_payload_cause_name(output_ctrl_change_cause_t cause);

/** @brief Compare two protection snapshots field by field. */
bool mqtt_svc_payload_protection_equal(const mqtt_svc_protection_t *a, const mqtt_svc_protection_t *b);

/**
 * @brief Build the `measurements` JSON with 4-decimal fixed-point numbers.
 *
 * @return Heap string to release with `mqtt_svc_payload_free()`, or NULL.
 */
char *mqtt_svc_payload_telemetry(const mqtt_svc_telemetry_t *telemetry, bool output_on);

/** @brief Build the `protection/state` JSON. */
char *mqtt_svc_payload_protection(const mqtt_svc_protection_t *protection);

/** @brief Build the device-based Home Assistant discovery JSON. */
char *mqtt_svc_payload_discovery(const mqtt_svc_topics_t *topics, const char *sw_version);

/** @brief Release a payload returned by this module. */
void mqtt_svc_payload_free(char *payload);

#ifdef __cplusplus
}
#endif
