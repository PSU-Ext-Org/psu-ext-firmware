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

# Included only when SSD1306 is enabled. Paths are relative to ssd1306/.
set(ssd1306_core_sources
    "core/ssd1306_views.c"
    "core/ssd1306_draw.c"
)
set(ssd1306_view_sources
    "views/ssd1306_builtin_views.c"
    "views/ssd1306_view_measurements.c"
    "views/ssd1306_view_status.c"
)
