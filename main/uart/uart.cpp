#include "uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "levitree_cpcd.pb-c.h"

#include <esp_log.h>
#include <driver/uart.h>
#include <esp_event.h>
#include <string.h>

#include "packet_processing.h"

static const char* TAG = "uart";

static QueueHandle_t uart0_queue;

uint8_t rxBuf[BUF_SIZE] = { 0 };
uint8_t xbeeRxBuf[BUF_SIZE] = { 0 };

uint16_t rxBufPos = 0;

//packets are
// 0xA5 - preamble
// 0x00 0x00 - message id
// 0x00 0x00 - message length
// ... payload
// 0x00 0x00 - crc16

void shiftRxBuf(uint8_t by_num)
{
    if (by_num >= rxBufPos) {
        rxBufPos = 0;
    }
    else {
        memmove(rxBuf, rxBuf + by_num, rxBufPos - by_num);
        rxBufPos -= by_num;
    }
}

void ProcessPacketsSerial_Task(void* pvParameter)
{
    LeviPacket_t* currPacket = nullptr;
    bool packet_complete = false;
    while (true) {
        if (rxBufPos >= 1 && (currPacket == nullptr || packet_complete)) {
            currPacket = (LeviPacket_t*)malloc(sizeof(LeviPacket_t));
            if (currPacket == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for packet");
                return;
            }
            // Initialize packet fields
            currPacket->got_preamble = false;
            currPacket->got_message_id = false;
            currPacket->got_message_length = false;
            currPacket->got_payload = false;
            currPacket->got_crc = false;
            currPacket->payload = NULL;
            currPacket->payload_cur_pos = 0;
            packet_complete = false;
        }

        if (currPacket == NULL) {
            vTaskDelay(10);
            continue;
        }

        // Preamble
        if (!currPacket->got_preamble && rxBufPos >= 1) {
            if (rxBuf[0] == 0xA5) {
                currPacket->preamble = 0xA5;
                currPacket->got_preamble = true;
                shiftRxBuf(1);
            }
            else {
                // Discard one byte and keep searching
                shiftRxBuf(1);
            }
        }

        // Message ID
        if (currPacket->got_preamble && !currPacket->got_message_id && rxBufPos >= 2) {
            memcpy(currPacket->message_id, rxBuf, 2);
            currPacket->got_message_id = true;
            shiftRxBuf(2);
        }

        // Message Length
        if (currPacket->got_message_id && !currPacket->got_message_length && rxBufPos >= 2) {
            memcpy(currPacket->message_length, rxBuf, 2);
            currPacket->got_message_length = true;
            shiftRxBuf(2);
        }

        // Payload (incremental copy)
        if (currPacket->got_message_length && !currPacket->got_payload) {
            uint16_t payload_size = (currPacket->message_length[0] << 8) | currPacket->message_length[1];

            if (currPacket->payload == NULL) {
                currPacket->payload = (uint8_t*)malloc(payload_size);
                if (currPacket->payload == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for payload");
                    return;
                }
            }

            // Calculate bytes to copy
            uint16_t bytes_to_copy = (rxBufPos < (payload_size - currPacket->payload_cur_pos)) ? rxBufPos : (payload_size - currPacket->payload_cur_pos);

            if (bytes_to_copy > 0) {
                memcpy(currPacket->payload + currPacket->payload_cur_pos, rxBuf, bytes_to_copy);
                currPacket->payload_cur_pos += bytes_to_copy;
                shiftRxBuf(bytes_to_copy);
            }

            if (currPacket->payload_cur_pos == payload_size) {
                currPacket->got_payload = true;
            }
        }

        // CRC
        if (currPacket->got_payload && !currPacket->got_crc && rxBufPos >= 2) {
            memcpy(currPacket->crc16, rxBuf, 2);
            currPacket->got_crc = true;
            shiftRxBuf(2);
        }

        // Packet complete
        if (currPacket->got_crc) {
            xTaskCreate(handle_packet_uart, "HandlePacket", 2048, currPacket, 5, NULL);
            packet_complete = true;
        }
    }
}

void ProcessPacketsXbee_Task(void* pvParameter)
{
    LeviPacket_t* currPacket = nullptr;
    bool packet_complete = false;
    while (true) {
        if (rxBufPos >= 1 && (currPacket == nullptr || packet_complete)) {
            currPacket = (LeviPacket_t*)malloc(sizeof(LeviPacket_t));
            if (currPacket == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for packet");
                return;
            }
            // Initialize packet fields
            currPacket->got_preamble = false;
            currPacket->got_message_id = false;
            currPacket->got_message_length = false;
            currPacket->got_payload = false;
            currPacket->got_crc = false;
            currPacket->payload = NULL;
            currPacket->payload_cur_pos = 0;
            packet_complete = false;
        }

        if (currPacket == NULL) {
            vTaskDelay(10);
            continue;
        }

        // Preamble
        if (!currPacket->got_preamble && rxBufPos >= 1) {
            if (rxBuf[0] == 0xA5) {
                currPacket->preamble = 0xA5;
                currPacket->got_preamble = true;
                shiftRxBuf(1);
            }
            else {
                // Discard one byte and keep searching
                shiftRxBuf(1);
            }
        }

        // Message ID
        if (currPacket->got_preamble && !currPacket->got_message_id && rxBufPos >= 2) {
            memcpy(currPacket->message_id, rxBuf, 2);
            currPacket->got_message_id = true;
            shiftRxBuf(2);
        }

        // Message Length
        if (currPacket->got_message_id && !currPacket->got_message_length && rxBufPos >= 2) {
            memcpy(currPacket->message_length, rxBuf, 2);
            currPacket->got_message_length = true;
            shiftRxBuf(2);
        }

        // Payload (incremental copy)
        if (currPacket->got_message_length && !currPacket->got_payload) {
            uint16_t payload_size = (currPacket->message_length[0] << 8) | currPacket->message_length[1];

            if (currPacket->payload == NULL) {
                currPacket->payload = (uint8_t*)malloc(payload_size);
                if (currPacket->payload == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for payload");
                    return;
                }
            }

            // Calculate bytes to copy
            uint16_t bytes_to_copy = (rxBufPos < (payload_size - currPacket->payload_cur_pos)) ? rxBufPos : (payload_size - currPacket->payload_cur_pos);

            if (bytes_to_copy > 0) {
                memcpy(currPacket->payload + currPacket->payload_cur_pos, rxBuf, bytes_to_copy);
                currPacket->payload_cur_pos += bytes_to_copy;
                shiftRxBuf(bytes_to_copy);
            }

            if (currPacket->payload_cur_pos == payload_size) {
                currPacket->got_payload = true;
            }
        }

        // CRC
        if (currPacket->got_payload && !currPacket->got_crc && rxBufPos >= 2) {
            memcpy(currPacket->crc16, rxBuf, 2);
            currPacket->got_crc = true;
            shiftRxBuf(2);
        }

        if (currPacket->got_crc) {
            xTaskCreate(handle_packet_uart, "HandlePacket", 2048, currPacket, 5, NULL);
            packet_complete = true;
        }
    }
}

static void uart_event_task(void* pvParameters)
{
    uart_event_t event;

    for (;;) {
        if (xQueueReceive(uart0_queue, (void*)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            case UART_DATA:
                ESP_LOGD(TAG, "[UART DATA]: %d", event.size);

                while (rxBufPos >= BUF_SIZE) {
                    vTaskDelay(1);
                }

                uart_read_bytes(UART_NUM, rxBuf + rxBufPos, event.size, portMAX_DELAY);
                rxBufPos += event.size;

                break;
                //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                uart_flush_input(UART_NUM);
                xQueueReset(uart0_queue);
                break;
            case UART_BUFFER_FULL:
                uart_flush_input(UART_NUM);
                xQueueReset(uart0_queue);
                break;
            case UART_PARITY_ERR:
                ESP_LOGE(TAG, "bad parity");
                break;
            case UART_FRAME_ERR:
                ESP_LOGE(TAG, "bad frame");
                break;
            default:
                break;
            }
        }
    }

    vTaskDelete(NULL);
}

void uart_init(void) {
#if HW_TYPE == HW_TYPE_MOTHERSHIP
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .source_clk = UART_SCLK_DEFAULT,
    };
    //Install UART driver, and get the queue.
    uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
    uart_param_config(UART_NUM, &uart_config);

    //Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(UART_NUM, TX_PIN, RX_PIN, RTS_PIN, CTS_PIN);

    //Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 3072, NULL, 12, NULL);
    //Create a task to process packets
    xTaskCreate(ProcessPacketsSerial_Task, "ProcessPackets_Task", 3072, NULL, 12, NULL);
#elif HW_TYPE == HW_TYPE_ROVER
    /* Configure parameters of an UART driver,
    * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .source_clk = UART_SCLK_DEFAULT,
    };
    //Install UART driver, and get the queue.
    uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
    uart_param_config(UART_NUM, &uart_config);

    //Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(UART_NUM, 2, 1, 3, 4);

    xTaskCreate(uart_event_task, "uart_event_task", 3072, NULL, 12, NULL);
    //Create a task to process packets
    xTaskCreate(ProcessPacketsXbee_Task, "ProcessPackets_Task_Xbee", 3072, NULL, 12, NULL);
#endif
}