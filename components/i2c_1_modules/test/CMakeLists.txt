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

# Optional tests obey the same switches as their production sources.
set(module_test_sources)
set(module_test_includes)
set(module_test_dependencies)
if(CONFIG_PSUEXT_I2C_1_SSD1306)
    list(APPEND module_test_sources "../ssd1306/test/test_ssd1306_views.c" "../ssd1306/test/ssd1306_test_view.c")
    list(APPEND module_test_includes "../ssd1306/include" "../ssd1306/private" "../ssd1306/views/private")
    list(APPEND module_test_dependencies
        unity scpi_handler i2c_1_modules measure_svc output_ctrl protection_svc wifi_manager usb_com tcp_server)
endif()
if(CONFIG_PSUEXT_I2C_1_MLX90614)
    list(APPEND module_test_sources
        "../mlx90614/test/test_mlx90614.c"
        "../mlx90614/test/test_mlx90614_driver.c")
    list(APPEND module_test_includes "../mlx90614/private" "../mlx90614/vendor")
    list(APPEND module_test_dependencies unity scpi_handler i2c_1_modules esp_driver_i2c)
endif()
if(NOT module_test_sources)
    idf_component_register()
    return()
endif()
idf_component_register(SRCS ${module_test_sources} PRIV_INCLUDE_DIRS ${module_test_includes})

# Menuconfig is unavailable during early dependency discovery. Link optional
# dependencies after registration, matching the production component.
foreach(dependency IN LISTS module_test_dependencies)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::${dependency})
endforeach()

if(CONFIG_PSUEXT_I2C_1_MLX90614)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=i2c_master_probe"
        "-Wl,--wrap=mlx90614_init"
        "-Wl,--wrap=mlx90614_deinit"
        "-Wl,--wrap=heap_caps_malloc"
        "-Wl,--wrap=i2c_master_bus_add_device"
        "-Wl,--wrap=i2c_master_bus_rm_device"
        "-Wl,--wrap=i2c_master_transmit_receive"
    )
endif()
