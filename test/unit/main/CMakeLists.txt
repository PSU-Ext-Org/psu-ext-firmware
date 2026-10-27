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
    SRCS "test_main.c" "../../support/test_nvs.c"
    PRIV_INCLUDE_DIRS "../../support"
    PRIV_REQUIRES unity nvs_flash
)

# Test-only isolation applies to all service and test translation units.
# Production firmware never links these wrappers.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=nvs_open"
    "-Wl,--wrap=nvs_open_from_partition"
    "-Wl,--wrap=nvs_flash_erase"
    "-Wl,--wrap=nvs_flash_erase_partition"
)
