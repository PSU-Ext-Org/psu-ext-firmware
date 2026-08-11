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
        "scpi_handler.c"
        "scpi_handler_calibration.c"
        "scpi_handler_common.c"
        "scpi_handler_measure.c"
        "scpi_handler_output.c"
        "scpi_handler_protection.c"
        "scpi_handler_system.c"
        "scpi_handler_timer.c"
        "scpi_handler_trigger.c"
        "scpi_handler_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos wifi_manager measure_svc output_ctrl protection_svc trigger_svc timer_svc timebase_svc
)
