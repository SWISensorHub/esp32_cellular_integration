/*
 * Amazon FreeRTOS Cellular Preview Release
 * Copyright (C) 20 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

#ifndef CELLULAR_DO_NOT_USE_CUSTOM_CONFIG
    /* Include custom config file before other headers. */
    #include "cellular_config.h"
#endif
#include "cellular_config_defaults.h"

/* Cellular comm interface include file. */
#include "cellular_comm_interface.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define COMM_IF_DEBUG 0
#if COMM_IF_DEBUG

#include <stdarg.h>
#include "esp_log.h"
static const char *COMM_IF_TAG = "COMM_IF_SIERRA";
#define ESP_LOGE(tag, format, ...)      ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define COMM_IF_LOGE(format, ...)  ESP_LOGE(COMM_IF_TAG, format, ##__VA_ARGS__)
#define COMM_IF_LOGW(format, ...)  ESP_LOGW(COMM_IF_TAG, format, ##__VA_ARGS__)
#define COMM_IF_LOGI(format, ...)  ESP_LOGI(COMM_IF_TAG, format, ##__VA_ARGS__)
#define COMM_IF_LOGD(format, ...)  ESP_LOGD(COMM_IF_TAG, format, ##__VA_ARGS__)
#define COMM_IF_LOGV(format, ...)  ESP_LOGV(COMM_IF_TAG, format, ##__VA_ARGS__)
#define COMM_IF_LOG_BUFFER_HEX(buffer, buff_len) ESP_LOG_BUFFER_HEX(COMM_IF_TAG, buffer, buff_len)


#else /* COMM_IF_DEBUG */

#define COMM_IF_LOGE(format, ...)
#define COMM_IF_LOGW(format, ...)
#define COMM_IF_LOGI(format, ...)
#define COMM_IF_LOGD(format, ...)
#define COMM_IF_LOGV(format, ...)
#define COMM_IF_LOG_BUFFER_HEX(buffer, buff_len)

#endif /* COMM_IF_DEBUG */


#define BX310X_HL78_TXD (GPIO_NUM_19)
#define BX310X_HL78_RXD (GPIO_NUM_22)
#define BX310X_HL78_RTS (GPIO_NUM_26)
#define BX310X_HL78_CTS (GPIO_NUM_27)


#define BX310X_HL78_UART (UART_NUM_1)

#define EVENTS_QUEUE_SIZE       (64)
#define BX310X_UART_BUF_SIZE    (2048)

/**
 * @brief A context of the communication interface.
 */
static struct
{
    CellularCommInterfaceReceiveCallback_t pRecvCB;
    void *pUserData;
    bool isOpen;
    TaskHandle_t event_task_handler;
} _CommIfContext = {
    .pRecvCB = NULL,
    .pUserData = NULL,
    .isOpen = false,
    .event_task_handler = NULL
};

static QueueHandle_t _CommIfQueue;

static void uart_event_task(void *pvParameters)
{
    CellularCommInterfaceError_t retComm = IOT_COMM_INTERFACE_SUCCESS;
    uart_event_t event;
    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(_CommIfQueue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
            // Event of UART receving data
            case UART_DATA:
                COMM_IF_LOGI("Data Event: size %d", event.size);
                if (_CommIfContext.pRecvCB != NULL) {
                    retComm = _CommIfContext.pRecvCB(
                        _CommIfContext.pUserData, (CellularCommInterfaceHandle_t)&_CommIfContext);
                    if (retComm == IOT_COMM_INTERFACE_SUCCESS) {
                        portYIELD_FROM_ISR();
                    }
                } else {
                    COMM_IF_LOGE("Data Event: Callback is null");
                }
                break;

            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                COMM_IF_LOGE("HW FIFO overflow");
                uart_flush_input(BX310X_HL78_UART);
                xQueueReset(_CommIfQueue);
                break;

            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
                COMM_IF_LOGE("Ring buffer full");
                uart_flush_input(BX310X_HL78_UART);
                xQueueReset(_CommIfQueue);
                break;

            // Event of UART RX break detected
            case UART_BREAK:
                COMM_IF_LOGE("UART RX break");
                break;

            // Event of UART parity check error
            case UART_PARITY_ERR:
                COMM_IF_LOGE("UART parity error");
                break;

            // Event of UART frame error
            case UART_FRAME_ERR:
                COMM_IF_LOGE("UART frame error");
                break;

            // UART_PATTERN_DET
            case UART_PATTERN_DET:
                COMM_IF_LOGI("Pattern-Det Event: size %d", event.size);
                if (_CommIfContext.pRecvCB != NULL) {
                    retComm = _CommIfContext.pRecvCB(
                        _CommIfContext.pUserData, (CellularCommInterfaceHandle_t)&_CommIfContext);
                    if (retComm == IOT_COMM_INTERFACE_SUCCESS) {
                        portYIELD_FROM_ISR();
                    }
                } else {
                    COMM_IF_LOGE("Pattern-Det Event: Callback is null");
                }
                break;

            // Others
            default:
                COMM_IF_LOGW("Unknown event type: %d", event.type);
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

static CellularCommInterfaceError_t _prvCommIntfOpen(
    CellularCommInterfaceReceiveCallback_t receiveCallback,
    void *pUserData,
    CellularCommInterfaceHandle_t *pCommInterfaceHandle)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 120,
        .use_ref_tick = 0x00,
    };

    if (_CommIfContext.isOpen) {
        COMM_IF_LOGE("Driver failure: port already open");
        return IOT_COMM_INTERFACE_FAILURE;
    }

    if (pCommInterfaceHandle == NULL) {
        COMM_IF_LOGE("Bad parameters: interface handler is NULL");
        return IOT_COMM_INTERFACE_BAD_PARAMETER;
    }

    _CommIfContext.pRecvCB = receiveCallback;
    _CommIfContext.pUserData = pUserData;
    *pCommInterfaceHandle = (CellularCommInterfaceHandle_t)&_CommIfContext;

    if (uart_driver_install(
            BX310X_HL78_UART,
            BX310X_UART_BUF_SIZE,
            BX310X_UART_BUF_SIZE,
            EVENTS_QUEUE_SIZE,
            &_CommIfQueue,
            0) != ESP_OK) {
        COMM_IF_LOGE("Failed to install driver");
        return IOT_COMM_INTERFACE_DRIVER_ERROR;
    }

    if (uart_param_config(BX310X_HL78_UART, &uart_config) != ESP_OK) {
        COMM_IF_LOGE("Failed to configure UART");
        return IOT_COMM_INTERFACE_DRIVER_ERROR;
    }

    if (uart_set_pin(
            BX310X_HL78_UART,
            BX310X_HL78_TXD,
            BX310X_HL78_RXD,
            BX310X_HL78_RTS,
            BX310X_HL78_CTS) != ESP_OK) {
        COMM_IF_LOGE("Failed to configure UART's pins");
        return IOT_COMM_INTERFACE_DRIVER_ERROR;
    }
    // Work around: CTS pin may not be asserted if a pull up is set
    ESP_ERROR_CHECK(gpio_set_pull_mode(BX310X_HL78_CTS, GPIO_FLOATING));

    _CommIfContext.isOpen = true;

    ESP_ERROR_CHECK(uart_enable_tx_intr(BX310X_HL78_UART, 1, 9));
    ESP_ERROR_CHECK(uart_enable_rx_intr(BX310X_HL78_UART));
    ESP_ERROR_CHECK(uart_set_rx_timeout(BX310X_HL78_UART, 9));

    // Create a task to handler UART events from ISR
    if (_CommIfContext.event_task_handler == NULL) {
        xTaskCreate(
            uart_event_task,
            "uart_event_task",
            2048,
            &_CommIfContext,
            tskIDLE_PRIORITY + 5,
            &_CommIfContext.event_task_handler);
    } else {
        COMM_IF_LOGE("Driver failure: event task was created");
        return IOT_COMM_INTERFACE_FAILURE;
    }

    return IOT_COMM_INTERFACE_SUCCESS;
}


static CellularCommInterfaceError_t _prvCommIntfClose(
    CellularCommInterfaceHandle_t commInterfaceHandle)
{
    if (_CommIfContext.event_task_handler != NULL) {
        vTaskDelete(_CommIfContext.event_task_handler);
        _CommIfContext.event_task_handler = NULL;
    }

    if (uart_driver_delete(BX310X_HL78_UART) != ESP_OK) {
        COMM_IF_LOGE("Failed to uninstall UART driver");
        return IOT_COMM_INTERFACE_DRIVER_ERROR;
    }

    _CommIfContext.isOpen = false;

    return IOT_COMM_INTERFACE_SUCCESS;
}


static CellularCommInterfaceError_t _prvCommIntfSend(
    CellularCommInterfaceHandle_t commInterfaceHandle,
    const uint8_t *pData,
    uint32_t dataLength,
    uint32_t timeoutMilliseconds,
    uint32_t *pDataSentLength)
{
    uint32_t len = 0;

    len = uart_write_bytes(BX310X_HL78_UART, (const char *)pData, dataLength);
    if (len != dataLength) {
        COMM_IF_LOGE("Send failed: buffer error");
        return IOT_COMM_INTERFACE_NO_MEMORY;
    }

    if (pDataSentLength != NULL) {
        *pDataSentLength = len;
    }

    if (uart_wait_tx_done(BX310X_HL78_UART, timeoutMilliseconds) != ESP_OK) {
        COMM_IF_LOGE("[CommIntfSend]: Timeout");
        return IOT_COMM_INTERFACE_TIMEOUT;
    }
    COMM_IF_LOGI("Sent: %d bytes\n", len);

    return IOT_COMM_INTERFACE_SUCCESS;
}

static CellularCommInterfaceError_t _prvCommIntfReceive(
    CellularCommInterfaceHandle_t commInterfaceHandle,
    uint8_t *pBuffer,
    uint32_t bufferLength,
    uint32_t timeoutMilliseconds,
    uint32_t *pDataReceivedLength)
{
    int len = 0;
    size_t avail;

    ESP_ERROR_CHECK(uart_get_buffered_data_len(BX310X_HL78_UART, &avail));
    avail = avail > bufferLength ? bufferLength : avail;
    len = uart_read_bytes(BX310X_HL78_UART, pBuffer, avail, timeoutMilliseconds);
    if (len < 0) {
        COMM_IF_LOGE("Receive error");
        return IOT_COMM_INTERFACE_DRIVER_ERROR;
    }

    if (pDataReceivedLength != NULL) {
        *pDataReceivedLength = len;
    }

    // If the amount of data read is equal to the buffer length, then we can't add the null
    // terminator necessary to print it.
    if (len < bufferLength) {
        pBuffer[len] = 0;
        COMM_IF_LOGI("\n====\n%s\n====\n", (char *)pBuffer);
    }
    COMM_IF_LOGI("Received %d bytes\n", *pDataReceivedLength);

    return IOT_COMM_INTERFACE_SUCCESS;
}

CellularCommInterface_t CellularCommInterface = {
    .open = _prvCommIntfOpen,
    .send = _prvCommIntfSend,
    .recv = _prvCommIntfReceive,
    .close = _prvCommIntfClose,
};
