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

#include <math.h>
#include <stdint.h>

/* Internal values use signed fractional u4 (1.0 = 0.0001 V, A or W).
 * Convert only at unsigned consumer boundaries. NaN maps to zero;
 * positive infinity saturates, and negative infinity maps to zero. */
static inline uint32_t measure_svc_precision_to_u4(double value)
{
    if (isnan(value) || value <= 0.0) {
        return 0U;
    }
    if (value >= (double)UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)(value + 0.5);
}
