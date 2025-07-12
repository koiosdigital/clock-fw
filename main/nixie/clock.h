#pragma once

#include <stdint.h>

// Nixie configuration structure
typedef struct {
    uint8_t brightness;      // 0-100 scale (maps to 40-100% duty cycle inverted)
    bool military_time;      // true for 24h, false for 12h format
    bool blinking_dots;      // true to blink separator dots with seconds
    bool enabled;            // true to enable nixie display, false to turn off
} nixie_config_t;

// Nixie configuration functions
void nixie_set_brightness(uint8_t brightness_percent);
uint8_t nixie_get_brightness(void);
void nixie_set_military_time(bool enabled);
bool nixie_get_military_time(void);
void nixie_set_blinking_dots(bool enabled);
bool nixie_get_blinking_dots(void);
void nixie_set_enabled(bool enabled);
bool nixie_get_enabled(void);
nixie_config_t nixie_get_config(void);
void nixie_set_config(const nixie_config_t* config);
void nixie_load_config_from_nvs(void);
void nixie_save_config_to_nvs(void);

void clock_init();
