#include "modbus.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "packet_processing.h"
#include "../def.h"
#include <string.h>

static const char* TAG = "modbus";

uint8_t rxBufMB[BUF_SIZE] = { 0 };
uint16_t rxBufPosMB = 0;
static QueueHandle_t uart1_queue;

void shiftRxBuf2(uint8_t by_num)
{
    if (by_num >= rxBufPosMB) {
        rxBufPosMB = 0;
    }
    else {
        memmove(rxBufMB, rxBufMB + by_num, rxBufPosMB - by_num);
        rxBufPosMB -= by_num;
    }
}

void ProcessPacketsModbus_Task(void* pvParameter)
{
    LeviPacket_t* currPacket = nullptr;
    bool packet_complete = false;
    while (true) {
        if (rxBufPosMB >= 1 && (currPacket == nullptr || packet_complete)) {
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
        if (!currPacket->got_preamble && rxBufPosMB >= 1) {
            if (rxBufMB[0] == 0xA5) {
                currPacket->preamble = 0xA5;
                currPacket->got_preamble = true;
                shiftRxBuf2(1);
            }
            else {
                // Discard one byte and keep searching
                shiftRxBuf2(1);
            }
        }

        // Message ID
        if (currPacket->got_preamble && !currPacket->got_message_id && rxBufPosMB >= 2) {
            memcpy(currPacket->message_id, rxBufMB, 2);
            currPacket->got_message_id = true;
            shiftRxBuf2(2);
        }

        // Message Length
        if (currPacket->got_message_id && !currPacket->got_message_length && rxBufPosMB >= 2) {
            memcpy(currPacket->message_length, rxBufMB, 2);
            currPacket->got_message_length = true;
            shiftRxBuf2(2);
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
            uint16_t bytes_to_copy = (rxBufPosMB < (payload_size - currPacket->payload_cur_pos)) ? rxBufPosMB : (payload_size - currPacket->payload_cur_pos);

            if (bytes_to_copy > 0) {
                memcpy(currPacket->payload + currPacket->payload_cur_pos, rxBufMB, bytes_to_copy);
                currPacket->payload_cur_pos += bytes_to_copy;
                shiftRxBuf2(bytes_to_copy);
            }

            if (currPacket->payload_cur_pos == payload_size) {
                currPacket->got_payload = true;
            }
        }

        // CRC
        if (currPacket->got_payload && !currPacket->got_crc && rxBufPosMB >= 2) {
            memcpy(currPacket->crc16, rxBufMB, 2);
            currPacket->got_crc = true;
            shiftRxBuf2(2);
        }

        // Packet complete
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
        if (xQueueReceive(uart1_queue, (void*)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            case UART_DATA:
                ESP_LOGD(TAG, "[UART DATA]: %d", event.size);

                while (rxBufPosMB >= BUF_SIZE) {
                    vTaskDelay(1);
                }

                uart_read_bytes(UART_NUM_1, rxBufMB + rxBufPosMB, event.size, portMAX_DELAY);
                rxBufPosMB += event.size;

                break;
                //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                uart_flush_input(UART_NUM_1);
                xQueueReset(uart1_queue);
                break;
            case UART_BUFFER_FULL:
                uart_flush_input(UART_NUM_1);
                xQueueReset(uart1_queue);
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

void modbus_init() {
    // Initialize UART for Modbus communication
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_NUM_1, BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
#if HW_TYPE == HW_TYPE_ROVER
    uart_set_pin(UART_NUM_1, 11, 10, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#elif HW_TYPE == HW_TYPE_MOTHERSHIP
    uart_set_pin(UART_NUM_1, 11, 10, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#else
#error "what are you doing?"
#endif

    xTaskCreate(uart_event_task, "uart_event_task_mb", 3072, NULL, 12, NULL);
    //Create a task to process packets
    xTaskCreate(ProcessPacketsModbus_Task, "ProcessModbus_task", 3072, NULL, 12, NULL);
}
LevitreeCpcd__RoutableMessage* write_mb_packet(LevitreeCpcd__RoutableMessage* packet);
void write_mb_packet_no_response(LevitreeCpcd__RoutableMessage* packet);