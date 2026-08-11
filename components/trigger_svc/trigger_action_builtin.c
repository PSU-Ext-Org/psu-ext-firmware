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
 * @file trigger_action_builtin.c
 * @brief Built-in trigger action implementations.
 */

#include "trigger_action_builtin.h"

#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "output_ctrl.h"
#include "protection_svc.h"
#include "timer_svc.h"

static const char *TAG = "trigger_action";

typedef struct {
    uint8_t channel;
    bool enable_output;
    bool toggle_output;
} trigger_action_output_params_t;

static const char *trigger_action_state_name(trigger_svc_state_t state)
{
    return state == TRIGGER_SVC_STATE_LOW ? "LOW" : "HIGH";
}

static void trigger_action_builtin_trim(char *value)
{
    size_t start = 0U;
    size_t end;

    if (value == NULL) {
        return;
    }

    end = strlen(value);
    while ((value[start] != '\0') && ((value[start] == ' ') || (value[start] == '\t'))) {
        start++;
    }

    while ((end > start) && ((value[end - 1U] == ' ') || (value[end - 1U] == '\t'))) {
        end--;
    }

    if (start > 0U) {
        memmove(value, value + start, end - start);
    }

    value[end - start] = '\0';
}

static void trigger_action_builtin_uppercase(char *value)
{
    if (value == NULL) {
        return;
    }

    for (size_t i = 0; value[i] != '\0'; ++i) {
        if ((value[i] >= 'a') && (value[i] <= 'z')) {
            value[i] = (char)(value[i] - ('a' - 'A'));
        }
    }
}

static bool trigger_action_builtin_parse_channel(const char *argument, uint8_t *channel)
{
    char argument_upper[TRIGGER_ACTION_BUILTIN_ARGUMENTS_MAX_LEN];

    if ((argument == NULL) || (channel == NULL)) {
        return false;
    }

    strlcpy(argument_upper, argument, sizeof(argument_upper));
    trigger_action_builtin_trim(argument_upper);
    trigger_action_builtin_uppercase(argument_upper);

    if ((strcmp(argument_upper, "1") == 0) || (strcmp(argument_upper, "CH1") == 0)) {
        *channel = 1U;
        return true;
    }

    return false;
}

static esp_err_t trigger_action_output_apply(
    const trigger_svc_event_t *event,
    const trigger_action_output_params_t *params)
{
    bool enabled;
    bool next_enabled;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(event != NULL, ESP_ERR_INVALID_ARG, TAG, "trigger event is null");
    ESP_RETURN_ON_FALSE(params != NULL, ESP_ERR_INVALID_ARG, TAG, "action params are null");

    if (params->toggle_output) {
        err = output_ctrl_get(params->channel, &enabled);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "toggle action read failed for CH%u: %s", params->channel, esp_err_to_name(err));
            return err;
        }
        next_enabled = !enabled;
    } else {
        next_enabled = params->enable_output;
    }

    ESP_LOGI(
        TAG,
        "output action pin IO%d state=%s at %" PRIu32 " ms -> CH%u %s",
        (int)event->pin,
        trigger_action_state_name(event->state),
        event->time_ms,
        params->channel,
        next_enabled ? "ON" : "OFF");

    if (next_enabled) {
        err = protection_svc_clear_trips(params->channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "clearing protection trips for CH%u failed: %s", params->channel, esp_err_to_name(err));
            return err;
        }
    }

    err = output_ctrl_set_with_cause(
        params->channel,
        next_enabled,
        OUTPUT_CTRL_CHANGE_CAUSE_TRIGGER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setting CH%u output failed: %s", params->channel, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t trigger_action_output_off_ch1_execute(const trigger_svc_event_t *event)
{
    static const trigger_action_output_params_t params = {
        .channel = 1U,
        .enable_output = false,
        .toggle_output = false,
    };

    return trigger_action_output_apply(event, &params);
}

static esp_err_t trigger_action_output_on_ch1_execute(const trigger_svc_event_t *event)
{
    static const trigger_action_output_params_t params = {
        .channel = 1U,
        .enable_output = true,
        .toggle_output = false,
    };

    return trigger_action_output_apply(event, &params);
}

static esp_err_t trigger_action_output_toggle_ch1_execute(const trigger_svc_event_t *event)
{
    static const trigger_action_output_params_t params = {
        .channel = 1U,
        .enable_output = false,
        .toggle_output = true,
    };

    return trigger_action_output_apply(event, &params);
}

static esp_err_t trigger_action_timer_toggle_ch1_execute(const trigger_svc_event_t *event)
{
    esp_err_t err;

    ESP_RETURN_ON_FALSE(event != NULL, ESP_ERR_INVALID_ARG, TAG, "trigger event is null");

    ESP_LOGI(
        TAG,
        "timer action pin IO%d state=%s at %" PRIu32 " ms -> CH1 toggle",
        (int)event->pin,
        trigger_action_state_name(event->state),
        event->time_ms);

    err = timer_svc_toggle_run_pause(TIMER_SVC_CHANNEL_CH1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "toggling CH1 timer failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static const trigger_action_t s_output_off_ch1_action = {
    .name = "OUT_OFF_CH1",
    .execute = trigger_action_output_off_ch1_execute,
};

static const trigger_action_t s_output_on_ch1_action = {
    .name = "OUT_ON_CH1",
    .execute = trigger_action_output_on_ch1_execute,
};

static const trigger_action_t s_output_toggle_ch1_action = {
    .name = "OUT_TOGGLE_CH1",
    .execute = trigger_action_output_toggle_ch1_execute,
};

static const trigger_action_t s_timer_toggle_ch1_action = {
    .name = "TIM_TOGGLE_CH1",
    .execute = trigger_action_timer_toggle_ch1_execute,
};

esp_err_t trigger_action_builtin_resolve(
    const char *function_name,
    const char *arguments,
    const trigger_action_t **action,
    char *normalized_function_name,
    size_t normalized_function_name_size,
    char *normalized_arguments,
    size_t normalized_arguments_size)
{
    char function_upper[TRIGGER_ACTION_BUILTIN_FUNCTION_NAME_MAX_LEN];
    char argument_copy[TRIGGER_ACTION_BUILTIN_ARGUMENTS_MAX_LEN];
    uint8_t channel;

    ESP_RETURN_ON_FALSE(function_name != NULL, ESP_ERR_INVALID_ARG, TAG, "function name is null");
    ESP_RETURN_ON_FALSE(arguments != NULL, ESP_ERR_INVALID_ARG, TAG, "arguments are null");
    ESP_RETURN_ON_FALSE(action != NULL, ESP_ERR_INVALID_ARG, TAG, "action pointer is null");

    strlcpy(function_upper, function_name, sizeof(function_upper));
    trigger_action_builtin_trim(function_upper);
    trigger_action_builtin_uppercase(function_upper);
    ESP_RETURN_ON_FALSE(function_upper[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "function name is empty");

    strlcpy(argument_copy, arguments, sizeof(argument_copy));
    trigger_action_builtin_trim(argument_copy);
    ESP_RETURN_ON_FALSE(argument_copy[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "arguments are empty");
    ESP_RETURN_ON_FALSE(strchr(argument_copy, ',') == NULL, ESP_ERR_INVALID_ARG, TAG, "too many trigger arguments");
    ESP_RETURN_ON_FALSE(
        trigger_action_builtin_parse_channel(argument_copy, &channel),
        ESP_ERR_INVALID_ARG,
        TAG,
        "unsupported trigger argument payload");
    ESP_RETURN_ON_FALSE(channel == 1U, ESP_ERR_INVALID_ARG, TAG, "unsupported trigger channel");

    if (strcmp(function_upper, "OUT_OFF") == 0) {
        *action = &s_output_off_ch1_action;
    } else if (strcmp(function_upper, "OUT_ON") == 0) {
        *action = &s_output_on_ch1_action;
    } else if (strcmp(function_upper, "OUT_TOGGLE") == 0) {
        *action = &s_output_toggle_ch1_action;
    } else if ((strcmp(function_upper, "TIM_START") == 0) ||
               (strcmp(function_upper, "TIM_PAUSE") == 0) ||
               (strcmp(function_upper, "TIM_TOGGLE") == 0)) {
        *action = &s_timer_toggle_ch1_action;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if ((normalized_function_name != NULL) && (normalized_function_name_size > 0U)) {
        strlcpy(normalized_function_name, function_upper, normalized_function_name_size);
    }

    if ((normalized_arguments != NULL) && (normalized_arguments_size > 0U)) {
        strlcpy(normalized_arguments, "CH1", normalized_arguments_size);
    }

    return ESP_OK;
}
