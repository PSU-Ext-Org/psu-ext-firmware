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
        "measure_prov_ads1115.c"
        "measure_svc_average.c"
        "measure_svc_calibration.c"
        "measure_svc_calibration_capture.c"
        "measure_svc_calibration_persistence.c"
        "measure_svc_calibration_record.c"
        "measure_svc_calibration_table.c"
        "measure_svc_event_bus.c"
        "measure_svc_history.c"
        "measure_svc_power.c"
        "measure_svc_sampler.c"
        "measure_svc.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2c esp_rom esp_timer nvs_flash
)
