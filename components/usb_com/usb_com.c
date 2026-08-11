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
 * @file usb_com.c
 * @brief USB CDC ACM transport for the PSU-EXT firmware.
 *
 * The implementation uses Espressif's TinyUSB integration to expose the
 * ESP32-S3 native USB peripheral as a virtual COM port. Incoming data is
 * accumulated into a line buffer and dispatched to the shared SCPI handler.
 */

#include "usb_com.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scpi_handler.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#define USB_COM_CDC_PORT TINYUSB_CDC_ACM_0
#define USB_COM_RX_TEMP_BUFFER_SIZE 64
#define USB_COM_COMMAND_BUFFER_SIZE 128
#define USB_COM_TX_BUFFER_SIZE 8192
#define USB_COM_TX_FLUSH_DELAY_TICKS pdMS_TO_TICKS(1)
#define USB_COM_TX_FLUSH_RETRY_LIMIT 250U
#define USB_COM_TX_QUEUE_WAIT_RETRY_LIMIT 250U
#define USB_COM_TX_TASK_STACK_SIZE 4096
#define USB_COM_TX_TASK_PRIORITY 5

static const char *TAG = "usb_com";

/**
 * @brief Internal runtime state of the USB CDC service.
 *
 * `command_buffer` accumulates one line of text at a time until a CR or LF
 * terminator is received.
 */
static struct {
    bool driver_ready;
    size_t command_length;
    char command_buffer[USB_COM_COMMAND_BUFFER_SIZE];
    TaskHandle_t tx_task_handle;
    SemaphoreHandle_t tx_lock;
    bool tx_pending;
    size_t tx_length;
    uint8_t tx_buffer[USB_COM_TX_BUFFER_SIZE];
} s_usb_com;

static bool usb_com_write_bytes(const uint8_t *data, size_t length)
{
    size_t tx_offset = 0U;
    uint32_t retries = 0U;

    if ((data == NULL) || (length == 0U)) {
        return false;
    }

    while (tx_offset < length) {
        size_t queued = tinyusb_cdcacm_write_queue(
            USB_COM_CDC_PORT,
            data + tx_offset,
            length - tx_offset);
        if (queued > 0U) {
            tx_offset += queued;
            retries = 0U;
            continue;
        }

        esp_err_t flush_err = tinyusb_cdcacm_write_flush(USB_COM_CDC_PORT, 0);
        if ((flush_err != ESP_OK) && (flush_err != ESP_ERR_NOT_FINISHED)) {
            ESP_LOGW(TAG, "USB CDC flush failed while queueing response: %s", esp_err_to_name(flush_err));
            return false;
        }

        if (retries++ >= USB_COM_TX_FLUSH_RETRY_LIMIT) {
            ESP_LOGW(TAG, "USB CDC queue stalled after %u retries", (unsigned)USB_COM_TX_FLUSH_RETRY_LIMIT);
            return false;
        }

        vTaskDelay(USB_COM_TX_FLUSH_DELAY_TICKS);
    }

    retries = 0U;
    while (true) {
        esp_err_t flush_err = tinyusb_cdcacm_write_flush(USB_COM_CDC_PORT, 0);
        if (flush_err == ESP_OK) {
            return true;
        }
        if (flush_err != ESP_ERR_NOT_FINISHED) {
            ESP_LOGW(TAG, "USB CDC final flush failed: %s", esp_err_to_name(flush_err));
            return false;
        }
        if (retries++ >= USB_COM_TX_FLUSH_RETRY_LIMIT) {
            ESP_LOGW(TAG, "USB CDC final flush timed out after %u retries", (unsigned)USB_COM_TX_FLUSH_RETRY_LIMIT);
            return false;
        }
        vTaskDelay(USB_COM_TX_FLUSH_DELAY_TICKS);
    }
}

static bool usb_com_queue_tx(const uint8_t *data, size_t length, bool append_line_ending)
{
    static const uint8_t line_ending[] = "\r\n";
    const size_t total_length = length + (append_line_ending ? (sizeof(line_ending) - 1U) : 0U);
    uint32_t retries = 0U;

    if ((data == NULL) || (length == 0U)) {
        return false;
    }
    if (total_length > sizeof(s_usb_com.tx_buffer)) {
        ESP_LOGW(TAG, "USB CDC response too large for TX buffer: %u bytes", (unsigned)total_length);
        return false;
    }
    if ((s_usb_com.tx_lock == NULL) || (s_usb_com.tx_task_handle == NULL)) {
        ESP_LOGW(TAG, "USB CDC TX path is not ready");
        return false;
    }

    while (true) {
        if (xSemaphoreTake(s_usb_com.tx_lock, portMAX_DELAY) != pdTRUE) {
            return false;
        }
        if (!s_usb_com.tx_pending) {
            memcpy(s_usb_com.tx_buffer, data, length);
            if (append_line_ending) {
                memcpy(s_usb_com.tx_buffer + length, line_ending, sizeof(line_ending) - 1U);
            }
            s_usb_com.tx_length = total_length;
            s_usb_com.tx_pending = true;
            xSemaphoreGive(s_usb_com.tx_lock);
            xTaskNotifyGive(s_usb_com.tx_task_handle);
            return true;
        }
        xSemaphoreGive(s_usb_com.tx_lock);

        if (retries++ >= USB_COM_TX_QUEUE_WAIT_RETRY_LIMIT) {
            ESP_LOGW(TAG, "USB CDC TX queue stayed busy for %u retries", (unsigned)USB_COM_TX_QUEUE_WAIT_RETRY_LIMIT);
            return false;
        }
        vTaskDelay(USB_COM_TX_FLUSH_DELAY_TICKS);
    }
}

static void usb_com_tx_task(void *arg)
{
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (true) {
            bool send_pending;
            size_t tx_length;

            if (xSemaphoreTake(s_usb_com.tx_lock, portMAX_DELAY) != pdTRUE) {
                break;
            }
            send_pending = s_usb_com.tx_pending;
            tx_length = s_usb_com.tx_length;

            if (!send_pending) {
                xSemaphoreGive(s_usb_com.tx_lock);
                break;
            }

            bool sent = usb_com_write_bytes(s_usb_com.tx_buffer, tx_length);
            s_usb_com.tx_pending = false;
            s_usb_com.tx_length = 0U;
            xSemaphoreGive(s_usb_com.tx_lock);

            if (!sent) {
                ESP_LOGW(TAG, "USB CDC TX task failed to send %u bytes", (unsigned)tx_length);
            }
        }
    }
}

/**
 * @brief Send a response line over the USB CDC interface.
 *
 * The function appends a CRLF line terminator to each response so common serial
 * terminals display the reply as a complete line.
 *
 * @param response Null-terminated response string to send. If `NULL`, nothing
 * is transmitted.
 */
static void usb_com_write_response(const char *response)
{
    if (response == NULL) {
        return;
    }

    if (!usb_com_queue_tx((const uint8_t *)response, strlen(response), true)) {
        ESP_LOGW(TAG, "USB CDC failed to queue text response");
    }
}

static void usb_com_write_binary_response(const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length == 0U)) {
        return;
    }

    if (!usb_com_queue_tx(data, length, false)) {
        ESP_LOGW(TAG, "USB CDC failed to queue binary response (%u bytes)", (unsigned)length);
    }
}

/**
 * @brief Feed received USB bytes into the line-oriented shared command parser.
 *
 * Characters are buffered until a CR or LF is received. At that point the
 * buffered command is terminated and dispatched to the shared handler.
 *
 * @param data Pointer to received bytes from TinyUSB.
 * @param length Number of bytes available in @p data.
 */
static void usb_com_process_rx_bytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        char ch = (char)data[i];

        if ((ch == '\r') || (ch == '\n')) {
            if (s_usb_com.command_length == 0) {
                continue;
            }

            s_usb_com.command_buffer[s_usb_com.command_length] = '\0';
            const scpi_handler_response_writer_t writer = {
                .write_text = usb_com_write_response,
                .write_binary = usb_com_write_binary_response,
            };
            scpi_handler_handle_command(s_usb_com.command_buffer, &writer);
            s_usb_com.command_length = 0;
            continue;
        }

        if (s_usb_com.command_length >= (USB_COM_COMMAND_BUFFER_SIZE - 1)) {
            ESP_LOGW(TAG, "Dropping oversized command");
            s_usb_com.command_length = 0;
            usb_com_write_response("ERR,\"Command too long\"");
            continue;
        }

        s_usb_com.command_buffer[s_usb_com.command_length++] = ch;
    }
}

/**
 * @brief TinyUSB CDC receive callback.
 *
 * The callback drains all currently available bytes from the CDC FIFO and
 * forwards them to the incremental line parser.
 *
 * @param itf CDC interface index reported by TinyUSB.
 * @param event CDC event payload. Unused by this callback.
 */
static void usb_com_rx_callback(int itf, cdcacm_event_t *event)
{
    uint8_t buffer[USB_COM_RX_TEMP_BUFFER_SIZE];
    size_t rx_size = 0;

    (void)event;

    while (tinyusb_cdcacm_read(itf, buffer, sizeof(buffer), &rx_size) == ESP_OK && rx_size > 0) {
        usb_com_process_rx_bytes(buffer, rx_size);
    }
}

/**
 * @brief TinyUSB callback for host line state changes.
 *
 * This is mainly useful for debugging terminal connection behavior such as DTR
 * and RTS assertion when a host application opens the COM port.
 *
 * @param itf CDC interface index reported by TinyUSB.
 * @param event Line state change event from TinyUSB.
 */
static void usb_com_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    ESP_LOGI(
        TAG,
        "CDC line state changed on port %d: DTR=%d RTS=%d",
        itf,
        event->line_state_changed_data.dtr,
        event->line_state_changed_data.rts);
}

static void usb_com_device_event_handler(tinyusb_event_t *event, void *arg)
{
    (void)arg;

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "USB attached to host");
        status_led_set_usb_failed(false);
        status_led_set_usb_connected(true);
        break;

    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "USB detached from host");
        status_led_set_usb_connected(false);
        break;

    // case TINYUSB_EVENT_SUSPENDED:
    //     ESP_LOGI(TAG, "USB suspended by host");
    //     status_led_set_usb_connected(false);
    //     break;

    // case TINYUSB_EVENT_RESUMED:
    //     ESP_LOGI(TAG, "USB resumed by host");
    //     status_led_set_usb_connected(true);
    //     break;

    default:
        break;
    }
}

void __attribute__((weak)) tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB device mounted by host");
    status_led_set_usb_failed(false);
    status_led_set_usb_connected(true);
}

void __attribute__((weak)) tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB device unmounted from host");
    status_led_set_usb_connected(false);
}

void __attribute__((weak)) tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGI(TAG, "USB suspended by host");
    status_led_set_usb_connected(false);
}

void __attribute__((weak)) tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB resumed by host");
    status_led_set_usb_failed(false);
    status_led_set_usb_connected(true);
}

esp_err_t usb_com_init(void)
{
    if (s_usb_com.driver_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(usb_com_device_event_handler);
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        status_led_set_usb_connected(false);
        status_led_set_usb_failed(true);
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = USB_COM_CDC_PORT,
        .callback_rx = usb_com_rx_callback,
        .callback_line_state_changed = usb_com_line_state_changed_callback,
    };

    err = tusb_cdc_acm_init(&acm_cfg);
    if (err != ESP_OK) {
        status_led_set_usb_connected(false);
        status_led_set_usb_failed(true);
        ESP_LOGE(TAG, "CDC ACM init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_usb_com.driver_ready = true;
    s_usb_com.command_length = 0;
    s_usb_com.tx_pending = false;
    s_usb_com.tx_length = 0U;
    s_usb_com.tx_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_usb_com.tx_lock != NULL, ESP_ERR_NO_MEM, TAG, "USB CDC TX mutex creation failed");
    BaseType_t task_created = xTaskCreate(
        usb_com_tx_task,
        "usb_com_tx",
        USB_COM_TX_TASK_STACK_SIZE,
        NULL,
        USB_COM_TX_TASK_PRIORITY,
        &s_usb_com.tx_task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "USB CDC TX task creation failed");
    status_led_set_usb_failed(false);
    status_led_set_usb_connected(false);

    ESP_LOGI(TAG, "USB CDC ready; send *IDN? over the enumerated COM port");
    return ESP_OK;
}
