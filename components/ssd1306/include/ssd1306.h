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
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the optional 128x64 measurement display on SDA GPIO6/SCL GPIO7.
 *
 * Call only from app_main after measurement sampling starts; concurrent calls
 * are not supported. Probes once per boot;
 * repeated calls return the initial result without creating another task.
 * A later communication failure disables the display until reboot.
 *
 * Initialization failures are logged and cleaned up inside this component.
 *
 * @return true on successful startup, false if absent or initialization failed.
 * Subsequent calls return the startup result,
 * even if the display task has since stopped after a communication failure.
 */
bool ssd1306_init(void);

#ifdef __cplusplus
}
#endif
