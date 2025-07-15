#pragma once

#include <stdint.h>

typedef struct {
    uint8_t brightness;      // 0-100%
    bool military_time;      // true for 24-hour format, false for 12-hour
    bool blinking_dots;      // true to enable blinking dots, false to disable
    bool on;                 // true if Nixie tubes are on, false if off
} nixie_config_t;

//Public
void nixie_clock_init();
void nixie_clock_task(void* pvParameters);
void nixie_apply_config(nixie_config_t* config);