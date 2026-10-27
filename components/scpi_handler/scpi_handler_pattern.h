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

typedef bool (*scpi_handler_pattern_visitor_t)(const char *keyword, void *context);

/* Patterns passed to match/visit must have passed validation. Visitors return
 * false to stop enumeration; visit returns false when stopped early. */
bool scpi_handler_pattern_valid(const char *pattern);
bool scpi_handler_pattern_match(const char *pattern, const char *keyword);
bool scpi_handler_pattern_visit(const char *pattern,
    scpi_handler_pattern_visitor_t visitor, void *context);
