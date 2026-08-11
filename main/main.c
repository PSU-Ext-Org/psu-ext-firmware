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

#define MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ 100U

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"
#include "clock_test.h"
#include "wifi_manager.h"
#include "status_led.h"
#include "usb_com.h"
#include "measure_svc.h"
#include "output_ctrl.h"
#include "protection_svc.h"
#include "wifi_connection.h"
#include "tcp_server.h"
#include "flash_store.h"
#include "trigger_config.h"
#include "trigger_svc.h"
#include "timer_svc.h"
#include "timebase_svc.h"

static const uint32_t MAIN_HARD_CURRENT_LIMIT_U4 = 25000U;
static const uint32_t MAIN_MAX_OVP_LIMIT_U4 = 300000U;
static const measure_svc_config_t MAIN_MEASURE_CONFIG = {
    /* Board revision 1.1.0: input voltage is mapped for future use but is not sampled yet. */
    .input_voltage_input = MEASURE_INPUT_ADS1115_AIN0,
    .output_voltage_input = MEASURE_INPUT_ADS1115_AIN1,
    .output_current_input = MEASURE_INPUT_ADS1115_AIN2,
};
uint32_t g_measurements_per_second_per_channel = MEASURE_SVC_DEFAULT_SAMPLE_RATE_HZ;

void app_main(void)
{

    // Set protection limits and initialize protection.
    const protection_svc_limits_t protection_limits = {
        .hard_current_limit_u4 = MAIN_HARD_CURRENT_LIMIT_U4,
        .max_ovp_limit_u4 = MAIN_MAX_OVP_LIMIT_U4,
    };

    printf("Initializing components test...\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay to init flash

    // ESP_ERROR_CHECK(flash_store_init());
   
    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(clock_test_init());
    ESP_ERROR_CHECK(timebase_svc_init());
    ESP_ERROR_CHECK(wifi_manager_init());

    ESP_ERROR_CHECK(output_ctrl_init());
    ESP_ERROR_CHECK(trigger_svc_init());
    ESP_ERROR_CHECK(trigger_config_init());
    
    ESP_ERROR_CHECK(measure_svc_init_with_config(&MAIN_MEASURE_CONFIG));
    ESP_ERROR_CHECK(measure_svc_set_sample_rate_hz(g_measurements_per_second_per_channel));
 
    ESP_ERROR_CHECK(protection_svc_init_with_limits(&protection_limits));
    ESP_ERROR_CHECK(timer_svc_init());
    ESP_ERROR_CHECK(measure_svc_start_sampling());

    ESP_ERROR_CHECK(usb_com_init());
    ESP_ERROR_CHECK(wifi_connection_init());
    ESP_ERROR_CHECK(tcp_server_init());

    printf("All components initialized.\n");
}
