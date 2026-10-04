# Copyright 2026 PSU-EXT Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

idf_component_register(
    SRCS
        "mqtt_svc.c"
        "mqtt_svc_command.c"
        "mqtt_svc_config.c"
        "mqtt_svc_format.c"
        "mqtt_svc_payload.c"
        "mqtt_svc_topics.c"
        "mqtt_svc_window.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_event
        esp_app_format
        esp_hw_support
        esp_netif
        esp_timer
        esp_wifi
        freertos
        mbedtls
        measure_svc
        nvs_flash
        output_ctrl
        protection_svc
        wifi_manager
)
