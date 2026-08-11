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
 * @file protection_svc.h
 * @brief CH1 over-voltage and over-current protection service.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTECTION_SVC_CHANNEL_CH0 0U
#define PROTECTION_SVC_CHANNEL_CH1 1U
#define PROTECTION_SVC_DEFAULT_OVP_U4 250000U
#define PROTECTION_SVC_DEFAULT_OCP_U4 25000U

typedef struct {
    uint32_t hard_current_limit_u4;
    uint32_t max_ovp_limit_u4;
} protection_svc_limits_t;

/**
 * @brief Initialize protection state, register listeners, and load persisted limits.
 */
esp_err_t protection_svc_init_with_limits(const protection_svc_limits_t *limits);

/**
 * @brief Update the CH0 or CH1 OVP threshold in volts scaled by 10,000.
 */
esp_err_t protection_svc_set_ovp_u4(uint8_t channel, uint32_t threshold_u4);

/**
 * @brief Read the CH0 or CH1 OVP threshold in volts scaled by 10,000.
 */
esp_err_t protection_svc_get_ovp_u4(uint8_t channel, uint32_t *threshold_u4);

/**
 * @brief Update the CH1 OCP threshold in amps scaled by 10,000.
 */
esp_err_t protection_svc_set_ocp_u4(uint8_t channel, uint32_t threshold_u4);

/**
 * @brief Read the CH1 OCP threshold in amps scaled by 10,000.
 */
esp_err_t protection_svc_get_ocp_u4(uint8_t channel, uint32_t *threshold_u4);

/**
 * @brief Enable or disable OCP checking for CH1.
 */
esp_err_t protection_svc_set_ocp_enabled(uint8_t channel, bool enabled);

/**
 * @brief Read whether OCP checking is enabled for CH1.
 */
esp_err_t protection_svc_get_ocp_enabled(uint8_t channel, bool *enabled);

/**
 * @brief Read the latched CH0 or CH1 OVP trip status.
 */
esp_err_t protection_svc_get_ovp_tripped(uint8_t channel, bool *tripped);

/**
 * @brief Read the latched CH1 OCP trip status.
 */
esp_err_t protection_svc_get_ocp_tripped(uint8_t channel, bool *tripped);

/**
 * @brief Clear latched protection status for CH0 or CH1.
 */
esp_err_t protection_svc_clear_trips(uint8_t channel);

/**
 * @brief Validate that CH1 may be enabled.
 *
 * This checks the latest calibrated CH0 input voltage against the CH0 OVP
 * threshold. On failure, the CH0 OVP latch is set and the relay must remain
 * off.
 */
esp_err_t protection_svc_check_ch1_enable_allowed(void);

#ifdef __cplusplus
}
#endif
