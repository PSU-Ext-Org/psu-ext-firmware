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

#include "../scpi_handler_arguments.h"
#include "unity.h"
#include <string.h>

TEST_CASE("argument reader preserves command boolean dialects", "[scpi][arguments]")
{
    const char *tokens[] = {"0", "1", "ON", "off", "2", "1;", "", "1,0"};
    for (unsigned i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        for (unsigned words = 0; words < 2; ++words) {
            char text[16];
            strlcpy(text, tokens[i], sizeof(text));
            scpi_handler_args_t args = scpi_handler_args(text, false);
            bool value;
            bool valid = scpi_handler_arg_bool(&args, words != 0, &value) &&
                scpi_handler_args_end(&args);
            TEST_ASSERT_EQUAL(i < 2 || (words && i < 4), valid);
            if (valid) {
                TEST_ASSERT_EQUAL(i == 1 || i == 2, value);
            }
        }
    }
}

TEST_CASE("argument reader preserves REL semicolons and rejects extra fields", "[scpi][arguments]")
{
    const char *inputs[] = {" ch1 , 0.00025; ", "CH0,0", "CH1,", "CH1,1,",
        "CH1,1,2", "CH1,1;;", "CH2,1", "CH1,nan", "CH1,inf", "CH1,-1", "CH1,1e20"};
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        char text[40];
        strlcpy(text, inputs[i], sizeof(text));
        scpi_handler_args_t args = scpi_handler_args(text, true);
        measure_channel_t channel;
        uint32_t value;
        bool valid = scpi_handler_arg_channel(&args, &channel) &&
            scpi_handler_arg_value(&args, &value) && scpi_handler_args_end(&args);
        TEST_ASSERT_EQUAL(i < 2, valid);
        if (i == 0) {
            TEST_ASSERT_EQUAL(MEASURE_CHANNEL_1, channel);
            TEST_ASSERT_EQUAL_UINT32(3, value);
        }
    }
    scpi_handler_args_t empty = scpi_handler_args(NULL, false);
    TEST_ASSERT_TRUE(scpi_handler_args_end(&empty));
    char *value;
    TEST_ASSERT_FALSE(scpi_handler_arg_string(&empty, &value));
    TEST_ASSERT_FALSE(scpi_handler_args_end(&empty));
}
