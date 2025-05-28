#include "packet_processing.h"
#include <def.h>

#include <esp_log.h>
#include <driver/uart.h>

#include "uart.h"
#include "levitree_cpcd.pb-c.h"

static const char* TAG = "packet_processing";

//MARK: PB handlers
LevitreeCpcd__RoutableMessage* handle_packet(uint8_t* packet, size_t len) {
    LevitreeCpcd__RoutableMessage* routable_message = levitree_cpcd__routable_message__unpack(NULL, len, packet);
    if (routable_message == NULL) {
        ESP_LOGE(TAG, "Failed to unpack routable message");
        return NULL;
    }

    return NULL;
}

//MARK: raw handlers
void handle_packet_uart(void* packet) {
    LeviPacket_t* currPacket = (LeviPacket_t*)packet;

    ESP_LOGI(TAG, "Received packet with ID: %02X%02X", currPacket->message_id[0], currPacket->message_id[1]);
    ESP_LOGI(TAG, "Payload size: %d", currPacket->payload_cur_pos);
    ESP_LOGI(TAG, "CRC: %02X%02X", currPacket->crc16[0], currPacket->crc16[1]);

    //check the crc
    uint16_t crc = 0;
    for (uint16_t i = 0; i < currPacket->payload_cur_pos; i++) {
        crc += currPacket->payload[i];
    }
    crc = ~crc + 1; // Two's complement
    if (crc == (currPacket->crc16[0] << 8 | currPacket->crc16[1])) {
        size_t len = (currPacket->message_length[0] << 8) | currPacket->message_length[1];
        LevitreeCpcd__RoutableMessage* response = handle_packet(currPacket->payload, len);
        if (response != NULL) {
            size_t response_len = levitree_cpcd__routable_message__get_packed_size(response);
            uint8_t* response_buf = (uint8_t*)malloc(response_len) + 7;
            if (response_buf != NULL) {
                response_buf[0] = 0xA5; // Preamble
                response_buf[1] = currPacket->message_id[0]; // Message ID (to be filled)
                response_buf[2] = currPacket->message_id[1]; // Message ID (to be filled)
                response_buf[3] = (response_len >> 8) & 0xFF; // Message Length (high byte)
                response_buf[4] = response_len & 0xFF; // Message Length (low byte)

                levitree_cpcd__routable_message__pack(response, response_buf + 5);

                uint16_t crc = 0;
                for (size_t i = 0; i < response_len + 5; i++) {
                    crc += response_buf[i];
                }
                crc = ~crc + 1; // Two's complement
                response_buf[response_len + 5] = (crc >> 8) & 0xFF; // CRC (high byte)
                response_buf[response_len + 6] = crc & 0xFF; // CRC (low byte)

                uart_write_bytes(UART_NUM, (const char*)response_buf, response_len + 7);
                free(response_buf);
            }
            else {
                ESP_LOGD(TAG, "packet response was null");
            }
            levitree_cpcd__routable_message__free_unpacked(response, NULL);
        }
    }

    free(currPacket->payload);
    free(currPacket);
    vTaskDelete(NULL);
}

void handle_packet_xbee(void* packet) {
    LeviPacket_t* currPacket = (LeviPacket_t*)packet;

    ESP_LOGI(TAG, "Received packet with ID: %02X%02X", currPacket->message_id[0], currPacket->message_id[1]);
    ESP_LOGI(TAG, "Payload size: %d", currPacket->payload_cur_pos);
    ESP_LOGI(TAG, "CRC: %02X%02X", currPacket->crc16[0], currPacket->crc16[1]);

    //check the crc
    uint16_t crc = 0;
    for (uint16_t i = 0; i < currPacket->payload_cur_pos; i++) {
        crc += currPacket->payload[i];
    }
    crc = ~crc + 1; // Two's complement
    if (crc == (currPacket->crc16[0] << 8 | currPacket->crc16[1])) {
        size_t len = (currPacket->message_length[0] << 8) | currPacket->message_length[1];
        LevitreeCpcd__RoutableMessage* response = handle_packet(currPacket->payload, len);
        if (response != NULL) {
            size_t response_len = levitree_cpcd__routable_message__get_packed_size(response);
            uint8_t* response_buf = (uint8_t*)malloc(response_len) + 7;
            if (response_buf != NULL) {
                response_buf[0] = 0xA5; // Preamble
                response_buf[1] = currPacket->message_id[0]; // Message ID (to be filled)
                response_buf[2] = currPacket->message_id[1]; // Message ID (to be filled)
                response_buf[3] = (response_len >> 8) & 0xFF; // Message Length (high byte)
                response_buf[4] = response_len & 0xFF; // Message Length (low byte)

                levitree_cpcd__routable_message__pack(response, response_buf + 5);

                uint16_t crc = 0;
                for (size_t i = 0; i < response_len + 5; i++) {
                    crc += response_buf[i];
                }
                crc = ~crc + 1; // Two's complement
                response_buf[response_len + 5] = (crc >> 8) & 0xFF; // CRC (high byte)
                response_buf[response_len + 6] = crc & 0xFF; // CRC (low byte)

                uart_write_bytes(UART_NUM, (const char*)response_buf, response_len + 7);
                free(response_buf);
            }
            levitree_cpcd__routable_message__free_unpacked(response, NULL);
        }
    }

    free(currPacket->payload);
    free(currPacket);
    vTaskDelete(NULL);
}