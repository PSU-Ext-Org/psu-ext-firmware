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

#include "scpi_handler_pattern.h"
#include "scpi_handler.h"

#include <string.h>

#include "unity.h"

TEST_CASE("SCPI patterns preserve coupled aliases and complete keyword boundaries", "[scpi][pattern]")
{
    const char *pattern = "[[:](SOURCE|SOUR):]OCP:(PROTECT:STATE|PROT:STAT)?";
    TEST_ASSERT_TRUE(scpi_handler_pattern_valid(pattern));
    const char *valid[] = {"OCP:PROTECT:STATE?", "OCP:PROT:STAT?",
        "SOURCE:OCP:PROTECT:STATE?", ":SOUR:OCP:PROT:STAT?"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        TEST_ASSERT_TRUE(scpi_handler_pattern_match(pattern, valid[i]));
    }
    const char *invalid[] = {":OCP:PROT:STAT?", "SOURCE:OCP:PROTECT:STAT?",
        "OCP:PROT:STATE?", "OCP:PROT:STAT", "OCP:PROT:STAT??", "OCP:PROT:STAT?X"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        TEST_ASSERT_FALSE(scpi_handler_pattern_match(pattern, invalid[i]));
    }
}

TEST_CASE("SCPI pattern validation bounds grammar and expansion", "[scpi][pattern]")
{
    const char *invalid[] = {"", "[", "(A)", "(A|)", "(|A)", "A|B", "[A|B]",
        "[A)", "(A|B]", "a", "A B", "A?B", "[A]", "[]", "A[[[[[B]]]]]",
        "A[B][C][D][E][F][G][H][I][J]"};
    TEST_ASSERT_FALSE(scpi_handler_pattern_valid(NULL));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        TEST_ASSERT_FALSE_MESSAGE(scpi_handler_pattern_valid(invalid[i]), invalid[i]);
    }
    TEST_ASSERT_TRUE(scpi_handler_pattern_valid("A[[[[B]]]]"));
    TEST_ASSERT_TRUE(scpi_handler_pattern_valid("A[B][C][D][E][F][G][H][I]"));
    TEST_ASSERT_TRUE(scpi_handler_pattern_valid("(A|B)[?]"));
    char boundary[129];
    memset(boundary, 'A', sizeof(boundary));
    boundary[127] = '\0';
    TEST_ASSERT_TRUE(scpi_handler_pattern_valid(boundary));
    boundary[127] = 'A';
    boundary[128] = '\0';
    TEST_ASSERT_FALSE(scpi_handler_pattern_valid(boundary));
}

static void pattern_callback(const char *argument,
    const scpi_handler_response_writer_t *writer, void *context)
{
    (void)argument;
    (void)writer;
    (void)context;
}

TEST_CASE("SCPI pattern and literal registrations reject overlaps atomically", "[scpi][registry]")
{
    static const scpi_handler_command_t pattern = {
        "TEST:PAT(TERN|)?", pattern_callback, NULL};
    /* Empty branches are invalid, even though the intended alias is nonempty. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_patterns(&pattern, 1));
    static const scpi_handler_command_t commands[] = {
        {"TEST:(PAT|PATTERN)?", pattern_callback, NULL},
    };
    static bool registered;
    if (!registered) {
        TEST_ASSERT_EQUAL(ESP_OK, scpi_handler_register_patterns(commands, 1));
        registered = true;
    }
    const scpi_handler_command_t literal = {"TEST:PAT?", pattern_callback, NULL};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, scpi_handler_register_commands(&literal, 1));
    const scpi_handler_command_t overlap[] = {
        {"TEST:(NEW|NEWER)?", pattern_callback, NULL},
        {"TEST:NEW?", pattern_callback, NULL},
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_patterns(overlap, 2));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(commands, 1));
}
