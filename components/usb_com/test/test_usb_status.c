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

/* USB startup failure injection; no physical USB driver is installed. */
#include "usb_com.h"

#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "unity.h"

void tud_mount_cb(void);
void tud_umount_cb(void);

static void (*s_line_state_callback)(int, cdcacm_event_t *);

esp_err_t __wrap_tinyusb_driver_install(const tinyusb_config_t *config)
{
    (void)config;
    /* Simulate a host that enumerates before usb_com_init() returns. */
    tud_mount_cb();
    return ESP_OK;
}

esp_err_t __wrap_tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config)
{
    s_line_state_callback = config->callback_line_state_changed;
    cdcacm_event_t event = {0};
    event.line_state_changed_data.dtr = true;
    s_line_state_callback(0, &event);
    return ESP_FAIL;
}

TEST_CASE("USB callbacks after initialization failure remain publicly disconnected", "[usb]")
{
    TEST_ASSERT_EQUAL(ESP_FAIL, usb_com_init());
    tud_mount_cb();
    cdcacm_event_t event = {0};
    event.line_state_changed_data.dtr = true;
    TEST_ASSERT_NOT_NULL(s_line_state_callback);
    s_line_state_callback(0, &event);
    usb_com_status_t status = usb_com_get_status();
    TEST_ASSERT_FALSE(status.mounted);
    TEST_ASSERT_FALSE(status.cdc_open);
    tud_umount_cb();

}
