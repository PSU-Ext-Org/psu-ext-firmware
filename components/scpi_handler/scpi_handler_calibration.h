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
#include "measure_types.h"
#include <stdbool.h>

static inline bool cal_channel_supported(measure_kind_t kind, measure_channel_t channel)
{
    return ((kind == MEASURE_KIND_VOLTAGE) &&
            ((channel == MEASURE_CHANNEL_0) || (channel == MEASURE_CHANNEL_1))) ||
        ((kind == MEASURE_KIND_CURRENT) && (channel == MEASURE_CHANNEL_1));
}
