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

#include "scpi_handler.h"

#include <stdbool.h>
#include <string.h>

#include "unity.h"

static char s_response[160];
static uint8_t s_binary;
static unsigned s_calls;
static int s_context;

static void capture_text(const char *text)
{
    strlcpy(s_response, text, sizeof(s_response));
}

static void capture_binary(const uint8_t *data, size_t length)
{
    TEST_ASSERT_EQUAL(1, length);
    s_binary = data[0];
}

static void extension_callback(
    const char *argument,
    const scpi_handler_response_writer_t *writer,
    void *context)
{
    TEST_ASSERT_EQUAL_PTR(&s_context, context);
    ++s_calls;
    writer->write_text(argument == NULL ? "NO ARG" : argument);
    if (writer->write_binary != NULL) {
        const uint8_t value = 42;
        writer->write_binary(&value, 1);
    }
}

static void run_command(const char *text)
{
    char command[180];
    const scpi_handler_response_writer_t writer = {
        .write_text = capture_text,
        .write_binary = capture_binary,
    };
    s_response[0] = '\0';
    strlcpy(command, text, sizeof(command));
    scpi_handler_handle_command(command, &writer);
}

TEST_CASE("SCPI extension tables register atomically and dispatch through transport writers", "[scpi][registry]")
{
    static bool registered;
    static const scpi_handler_command_t commands[] = {
        {"TEST:EXT?", extension_callback, &s_context},
        {"TEST:EXTENSION?", extension_callback, &s_context},
        {"*IDN?", extension_callback, &s_context},
    };
    if (!registered) {
        TEST_ASSERT_EQUAL(ESP_OK, scpi_handler_register_commands(commands, 3));
        registered = true;
    }
    s_calls = 0;
    run_command("  test:ext? MixedCase argument  ");
    TEST_ASSERT_EQUAL_STRING("MixedCase argument", s_response);
    TEST_ASSERT_EQUAL(42, s_binary);
    TEST_ASSERT_EQUAL(1, s_calls);
    run_command("TEST:EXTENSION?");
    TEST_ASSERT_EQUAL_STRING("NO ARG", s_response);
    TEST_ASSERT_EQUAL(2, s_calls);
    run_command("*IDN?");
    TEST_ASSERT_EQUAL(2, s_calls);
    TEST_ASSERT_NOT_NULL(strstr(s_response, "PSU-EXT"));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, scpi_handler_register_commands(commands, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(NULL, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(commands, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, scpi_handler_register_commands(commands, SCPI_HANDLER_MAX_REGISTERED_COMMANDS + 1));

    const scpi_handler_command_t invalid[] = {
        {"TEST:UNPUBLISHED?", extension_callback, &s_context},
        {"lowercase?", extension_callback, &s_context},
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(invalid, 2));
    run_command("TEST:UNPUBLISHED?");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Unknown command\"", s_response);

    const scpi_handler_command_t conflict[] = {
        {"TEST:UNPUBLISHED?", extension_callback, &s_context},
        {"TEST:EXT?", extension_callback, &s_context},
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, scpi_handler_register_commands(conflict, 2));
    run_command("TEST:UNPUBLISHED?");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Unknown command\"", s_response);

    const scpi_handler_command_t duplicates[] = {
        {"TEST:DUP?", extension_callback, &s_context},
        {"TEST:DUP?", extension_callback, &s_context},
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(duplicates, 2));
    const scpi_handler_command_t missing_callback = {"TEST:BAD?", NULL, NULL};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, scpi_handler_register_commands(&missing_callback, 1));
    char oversized[140];
    memset(oversized, 'A', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    run_command(oversized);
    TEST_ASSERT_EQUAL_STRING("ERR,\"Command keyword too long\"", s_response);

}

TEST_CASE("built-in aliases retain their exact accepted spelling", "[scpi][registry]")
{
    const char *valid[] = {":CALIBRATION:VOLT:COUNT?", "CAL:VOLTAGE:COUN?",
        ":SOUR:OVP", "SOURCE:OCP:STAT", "SOUR:RESET:PROT", ":SOURCE:OVP",
        ":TIMER:CLE", "TIM:ADD", ":SYSTEM:DATETIME", "MEASURE:ADC:RATE",
        ":SYSTEM:MQTT:ENABLE", "SYST:MQTT:ENAB", "MEAS:VOLT:REL"};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        run_command(valid[i]);
        TEST_ASSERT_EQUAL_MEMORY("ERR,", s_response, 4);
        TEST_ASSERT_NULL(strstr(s_response, "Unknown command"));
    }
    const char *invalid[] = {":OUTP", "MEASURE:VOLT?", "CALIB:TRAN?", "SYST:ERROR?",
        "CAL:VOLT:COUNT", "MEAS:VOLT:REL??", "SOURCE:OVP:PROTECT:STAT?"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        run_command(invalid[i]);
        TEST_ASSERT_NOT_NULL(strstr(s_response, "Unknown command"));
    }
    run_command("WIFI:SSID");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Missing SSID\"", s_response);
    run_command("WIFI:PASS");
    TEST_ASSERT_EQUAL_STRING("ERR,\"Missing password\"", s_response);
}
