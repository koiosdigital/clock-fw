#pragma once

#include <stdint.h>

#define HW_TYPE_MOTHERSHIP 1
#define HW_TYPE_ROVER 2

typedef struct LeviPacket_t {
    uint8_t preamble = 0;
    uint8_t message_id[2] = { 0, 0 };
    uint8_t message_length[2] = { 0, 0 };
    uint8_t* payload = nullptr;
    uint8_t crc16[2] = { 0, 0 };

    //internal state
    bool got_preamble = false;
    bool got_message_id = false;
    bool got_message_length = false;
    uint16_t payload_cur_pos = 0;
    bool got_payload = false;
    bool got_crc = false;
} LeviPacket_t;

typedef struct XbeeFrame_t {
    uint8_t start_delimiter;
    uint8_t length[2];
    uint8_t frame_type;
    uint8_t frame_id;
    uint8_t* data = nullptr;
    uint8_t checksum;
} XbeeFrame_t;