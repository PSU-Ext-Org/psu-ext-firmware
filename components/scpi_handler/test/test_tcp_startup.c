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

#include "tcp_server.h"

#include "esp_event.h"
#include "unity.h"
#include "wifi_manager.h"

/* Run this case first after reset to exercise startup without a default loop.
 * It does not configure WiFi or change stored credentials. */
TEST_CASE("TCP startup creates or reuses the default event loop", "[tcp][startup]")
{
    TEST_ASSERT_EQUAL(ESP_OK, wifi_manager_init());
    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_event_loop_create_default());
    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_init());
    TEST_ASSERT_FALSE(tcp_server_session_is_active());
}
