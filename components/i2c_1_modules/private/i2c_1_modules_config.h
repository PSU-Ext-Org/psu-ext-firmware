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

#include "sdkconfig.h"

/* MLX90614 specifies at most 100 kHz. All devices on the shared wires use
 * that rate when it is enabled; display-only configurations retain 400 kHz. */
#ifdef CONFIG_PSUEXT_I2C_1_MLX90614
#define I2C_1_MODULES_CLOCK_HZ 100000
#else
#define I2C_1_MODULES_CLOCK_HZ 400000
#endif
