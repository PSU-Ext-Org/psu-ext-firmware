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
 * @file clock_test.c
 * @brief Boot-time proof that RTC_SLOW_CLK is sourced from the 32.768 kHz crystal.
 */

#include "clock_test.h"

#include <stdint.h>

#include "esp_clk_tree.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "soc/clk_tree_defs.h"

#define CLOCK_TEST_RTC_SLOW_MIN_HZ 32000U
#define CLOCK_TEST_RTC_SLOW_MAX_HZ 33500U

static const char *TAG = "clock_test";

static const char *clock_test_configured_source(void)
{
#if CONFIG_RTC_CLK_SRC_EXT_CRYS
    return "external 32.768 kHz crystal";
#elif CONFIG_RTC_CLK_SRC_EXT_OSC
    return "external RTC oscillator";
#elif CONFIG_RTC_CLK_SRC_INT_8MD256
    return "internal 8 MHz / 256";
#elif CONFIG_RTC_CLK_SRC_INT_RC
    return "internal RC";
#else
    return "unknown";
#endif
}

esp_err_t clock_test_init(void)
{
    uint32_t rtc_slow_hz = 0;

    ESP_LOGI(TAG, "RTC slow clock configured source: %s", clock_test_configured_source());

#if !CONFIG_RTC_CLK_SRC_EXT_CRYS
    ESP_LOGW(TAG, "RTC slow clock is not configured for the external 32.768 kHz crystal");
#endif

    esp_err_t err = esp_clk_tree_src_get_freq_hz(
        SOC_MOD_CLK_RTC_SLOW,
        ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT,
        &rtc_slow_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC slow clock frequency measurement failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "RTC slow clock measured frequency: %lu Hz", (unsigned long)rtc_slow_hz);

    if ((rtc_slow_hz >= CLOCK_TEST_RTC_SLOW_MIN_HZ) && (rtc_slow_hz <= CLOCK_TEST_RTC_SLOW_MAX_HZ)) {
        ESP_LOGI(TAG, "RTC slow clock test PASS");
        return ESP_OK;
    }

    ESP_LOGE(
        TAG,
        "RTC slow clock test FAIL: expected %lu..%lu Hz",
        (unsigned long)CLOCK_TEST_RTC_SLOW_MIN_HZ,
        (unsigned long)CLOCK_TEST_RTC_SLOW_MAX_HZ);
    return ESP_FAIL;
}
