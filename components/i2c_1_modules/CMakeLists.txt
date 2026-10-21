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

set(module_sources "i2c_1_modules.c")
set(module_dependencies)
set(module_include_dirs "include")
set(module_private_include_dirs "private")
if(CONFIG_PSUEXT_I2C_1_ENABLED)
    list(APPEND module_dependencies esp_driver_i2c)
endif()
if(CONFIG_PSUEXT_I2C_1_SSD1306)
    include("${CMAKE_CURRENT_LIST_DIR}/ssd1306/sources.cmake")
    list(APPEND module_sources "ssd1306/core/ssd1306.c")
    foreach(source IN LISTS ssd1306_core_sources ssd1306_view_sources)
        list(APPEND module_sources "ssd1306/${source}")
    endforeach()
    list(APPEND module_include_dirs "ssd1306/include")
    list(APPEND module_private_include_dirs "ssd1306/private" "ssd1306/views/private")
    list(APPEND module_dependencies
        measure_svc output_ctrl protection_svc esp_driver_gpio esp_timer
        scpi_handler wifi_manager usb_com tcp_server
    )
endif()

if(CONFIG_PSUEXT_I2C_1_MLX90614)
    list(APPEND module_sources
        "mlx90614/mlx90614_module.c"
        "mlx90614/mlx90614_commands.c"
        "mlx90614/mlx90614_data.c"
        "mlx90614/vendor/mlx90614.c"
    )
    list(APPEND module_private_include_dirs "mlx90614/private" "mlx90614/vendor")
    list(APPEND module_dependencies scpi_handler esp_timer)
endif()

idf_component_register(
    SRCS ${module_sources}
    INCLUDE_DIRS ${module_include_dirs}
    PRIV_INCLUDE_DIRS ${module_private_include_dirs}
)

# CONFIG_* is unavailable during IDF's early dependency expansion. Link enabled
# dependencies after registration, when configuration is loaded. These services
# are included by the application's main component (and the target test app).
foreach(dependency IN LISTS module_dependencies)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::${dependency})
endforeach()
