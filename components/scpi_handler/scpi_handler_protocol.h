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

#define SCPI_HANDLER_COMMAND_BUFFER_SIZE 128
#define SCPI_HANDLER_VALUE_SCALE_U4 10000U
typedef struct {
    char *keyword;
    char *argument;
} scpi_handler_parsed_command_t;

void scpi_handler_trim(char *command);
void scpi_handler_uppercase(char *command);
void scpi_handler_parse_command(char *command, scpi_handler_parsed_command_t *parsed);
