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

# Honor menuconfig in test builds too: a disabled display contributes no
# test sources, private include paths, or display-specific dependencies.
if(NOT CONFIG_PSUEXT_I2C_1_SSD1306)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS "test_ssd1306_views.c" "ssd1306_test_view.c"
    PRIV_INCLUDE_DIRS
        "../ssd1306/include"
        "../ssd1306/private"
        "../ssd1306/views/private"
)

# CONFIG_* is unavailable during early dependency discovery. Link only after
# registration, when menuconfig is loaded, as the production module does.
foreach(dependency unity scpi_handler i2c_1_modules measure_svc output_ctrl protection_svc wifi_manager usb_com tcp_server)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::${dependency})
endforeach()
