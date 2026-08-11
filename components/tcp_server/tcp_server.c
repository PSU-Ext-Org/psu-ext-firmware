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

/**
 * @file tcp_server.c
 * @brief TCP SCPI server for PSU-EXT over WiFi.
 */

#include "tcp_server.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "scpi_handler.h"
#include "wifi_manager.h"

#define TCP_SERVER_PORT 5025
#define TCP_SERVER_LISTEN_BACKLOG 1
#define TCP_SERVER_TASK_STACK_SIZE 4096
#define TCP_SERVER_TASK_PRIORITY 4
#define TCP_SERVER_POLL_DELAY_MS 500
#define TCP_SERVER_CLIENT_RECV_TIMEOUT_MS 500
#define TCP_SERVER_RX_BUFFER_SIZE 256
#define TCP_SERVER_COMMAND_BUFFER_SIZE 128
#define TCP_SERVER_CTRL_C_GRACE_PERIOD_MS 1000U

static const char *TAG = "tcp_server";

static struct {
    bool initialized;
    TaskHandle_t task_handle;
} s_tcp_server;

static struct {
    int active_client_socket;
} s_tcp_server_client;

/**
 * @brief Remove stray leading TCP noise characters from a completed command.
 *
 * Some terminal clients can inject a leading apostrophe before the first real
 * command text. Strip that here so the shared SCPI handler sees the intended
 * command keyword.
 *
 * @param command Mutable null-terminated command buffer.
 */
static void tcp_server_sanitize_command(char *command)
{
    if (command == NULL) {
        return;
    }

    while ((*command == '\'') || (*command == ' ')) {
        memmove(command, command + 1, strlen(command));
    }
}

/**
 * @brief Check whether WiFi is currently connected.
 *
 * @return `true` if the runtime WiFi state reports a connected station.
 */
static bool tcp_server_is_wifi_connected(void)
{
    wifi_manager_state_t state;
    esp_err_t err = wifi_manager_get_state(&state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read WiFi state: %s", esp_err_to_name(err));
        return false;
    }

    return state.is_connected;
}

/**
 * @brief Wait until WiFi is connected before opening the server socket.
 */
static void tcp_server_wait_for_wifi(void)
{
    while (!tcp_server_is_wifi_connected()) {
        vTaskDelay(pdMS_TO_TICKS(TCP_SERVER_POLL_DELAY_MS));
    }
}

/**
 * @brief Create and bind the listening socket on TCP port 5025.
 *
 * @param listen_socket Output pointer receiving the created socket descriptor.
 *
 * @return
 * - `ESP_OK` if the socket is ready to accept clients
 * - `ESP_FAIL` if socket creation, bind, or listen fails
 */
static esp_err_t tcp_server_open_listener(int *listen_socket)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_SERVER_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int enable = 1;

    *listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (*listen_socket < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return ESP_FAIL;
    }

    if (setsockopt(*listen_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
        ESP_LOGW(TAG, "setsockopt(SO_REUSEADDR) failed: errno=%d", errno);
    }

    if (bind(*listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed on port %d: errno=%d", TCP_SERVER_PORT, errno);
        close(*listen_socket);
        *listen_socket = -1;
        return ESP_FAIL;
    }

    if (listen(*listen_socket, TCP_SERVER_LISTEN_BACKLOG) != 0) {
        ESP_LOGE(TAG, "listen() failed: errno=%d", errno);
        close(*listen_socket);
        *listen_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Listening on TCP port %d", TCP_SERVER_PORT);
    return ESP_OK;
}

/**
 * @brief Send one SCPI response line to the active TCP client.
 *
 * Responses are terminated with LF so line-oriented TCP clients can process
 * the shared SCPI handler output in the same command/response style.
 *
 * @param response Null-terminated response string to transmit.
 */
static void tcp_server_write_response(const char *response)
{
    static const char line_ending[] = "\r\n";

    if ((response == NULL) || (s_tcp_server_client.active_client_socket < 0)) {
        return;
    }

    size_t response_length = strlen(response);
    size_t tx_offset = 0;

    while (tx_offset < response_length) {
        int tx_length = send(
            s_tcp_server_client.active_client_socket,
            response + tx_offset,
            response_length - tx_offset,
            0);
        if (tx_length <= 0) {
            ESP_LOGW(TAG, "send() failed while writing response: errno=%d", errno);
            return;
        }

        tx_offset += (size_t)tx_length;
    }

    if (send(
            s_tcp_server_client.active_client_socket,
            line_ending,
            sizeof(line_ending) - 1U,
            0) <= 0) {
        ESP_LOGW(TAG, "send() failed while writing line ending: errno=%d", errno);
    }
}

static void tcp_server_write_binary_response(const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length == 0U) || (s_tcp_server_client.active_client_socket < 0)) {
        return;
    }

    size_t tx_offset = 0;
    while (tx_offset < length) {
        int tx_length = send(
            s_tcp_server_client.active_client_socket,
            data + tx_offset,
            length - tx_offset,
            0);
        if (tx_length <= 0) {
            ESP_LOGW(TAG, "send() failed while writing binary response: errno=%d", errno);
            return;
        }

        tx_offset += (size_t)tx_length;
    }
}

/**
 * @brief Serve one TCP client until disconnect, Ctrl+C, or WiFi loss.
 *
 * @param client_socket Accepted client socket descriptor.
 */
static void tcp_server_handle_client(int client_socket)
{
    uint8_t buffer[TCP_SERVER_RX_BUFFER_SIZE];
    char command_buffer[TCP_SERVER_COMMAND_BUFFER_SIZE];
    size_t command_length = 0;
    const int64_t connected_at_us = esp_timer_get_time();
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = TCP_SERVER_CLIENT_RECV_TIMEOUT_MS * 1000,
    };

    s_tcp_server_client.active_client_socket = client_socket;

    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGW(TAG, "setsockopt(SO_RCVTIMEO) failed: errno=%d", errno);
    }

    for (;;) {
        int rx_length;

        if (!tcp_server_is_wifi_connected()) {
            ESP_LOGW(TAG, "Closing client because WiFi disconnected");
            break;
        }

        rx_length = recv(client_socket, buffer, sizeof(buffer), 0);
        if (rx_length > 0) {
            const uint32_t connected_for_ms = (uint32_t)((esp_timer_get_time() - connected_at_us) / 1000ULL);
            if (connected_for_ms >= TCP_SERVER_CTRL_C_GRACE_PERIOD_MS) {
                bool ctrl_c_only = true;
                bool ctrl_c_seen = false;

                for (int i = 0; i < rx_length; ++i) {
                    if (buffer[i] == 0x03U) {
                        ctrl_c_seen = true;
                        continue;
                    }

                    if ((buffer[i] == '\r') || (buffer[i] == '\n')) {
                        continue;
                    }

                    ctrl_c_only = false;
                    break;
                }

                if (ctrl_c_seen && ctrl_c_only) {
                    ESP_LOGI(TAG, "Received standalone Ctrl+C, closing client connection");
                    s_tcp_server_client.active_client_socket = -1;
                    return;
                }
            }

            for (int i = 0; i < rx_length; ++i) {
                char ch = (char)buffer[i];

                if (ch == '\r') {
                    continue;
                }

                if (ch == '\n') {
                    if (command_length == 0U) {
                        continue;
                    }

                    command_buffer[command_length] = '\0';
                    tcp_server_sanitize_command(command_buffer);
                    if (command_buffer[0] == '\0') {
                        command_length = 0;
                        continue;
                    }
                    const scpi_handler_response_writer_t writer = {
                        .write_text = tcp_server_write_response,
                        .write_binary = tcp_server_write_binary_response,
                    };
                    scpi_handler_handle_command(command_buffer, &writer);
                    command_length = 0;
                    continue;
                }

                if (((unsigned char)ch < 0x20U) || ((unsigned char)ch > 0x7EU)) {
                    continue;
                }

                if ((command_length == 0U) && (ch == '\'')) {
                    continue;
                }

                if (command_length >= (TCP_SERVER_COMMAND_BUFFER_SIZE - 1U)) {
                    ESP_LOGW(TAG, "Dropping oversized TCP command");
                    command_length = 0;
                    tcp_server_write_response("ERR,\"Command too long\"");
                    continue;
                }

                command_buffer[command_length++] = ch;
            }
            continue;
        }

        if (rx_length == 0) {
            ESP_LOGI(TAG, "Client disconnected");
            break;
        }

        if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
            continue;
        }

        ESP_LOGW(TAG, "recv() failed: errno=%d", errno);
        break;
    }

    s_tcp_server_client.active_client_socket = -1;
}

/**
 * @brief Main TCP server task.
 *
 * The task blocks until WiFi is available, serves one TCP client at a time,
 * and re-enters the wait loop after disconnects or WiFi loss.
 *
 * @param arg Unused task argument.
 */
static void tcp_server_task(void *arg)
{
    (void)arg;

    for (;;) {
        int listen_socket = -1;

        tcp_server_wait_for_wifi();

        if (tcp_server_open_listener(&listen_socket) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(TCP_SERVER_POLL_DELAY_MS));
            continue;
        }

        while (tcp_server_is_wifi_connected()) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_socket = accept(listen_socket, (struct sockaddr *)&client_addr, &client_addr_len);

            if (client_socket < 0) {
                if (!tcp_server_is_wifi_connected()) {
                    break;
                }

                ESP_LOGW(TAG, "accept() failed: errno=%d", errno);
                vTaskDelay(pdMS_TO_TICKS(TCP_SERVER_POLL_DELAY_MS));
                continue;
            }

            ESP_LOGI(
                TAG,
                "Client connected from %s:%d",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port));
            tcp_server_handle_client(client_socket);
            close(client_socket);
        }

        if (listen_socket >= 0) {
            close(listen_socket);
        }
    }
}

esp_err_t tcp_server_init(void)
{
    if (s_tcp_server.initialized) {
        return ESP_OK;
    }

    s_tcp_server_client.active_client_socket = -1;

    BaseType_t task_created = xTaskCreate(
        tcp_server_task,
        "tcp_server",
        TCP_SERVER_TASK_STACK_SIZE,
        NULL,
        TCP_SERVER_TASK_PRIORITY,
        &s_tcp_server.task_handle);

    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "tcp server task creation failed");

    s_tcp_server.initialized = true;
    ESP_LOGI(TAG, "TCP server initialized");
    return ESP_OK;
}
