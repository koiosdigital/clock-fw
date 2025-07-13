#pragma once

#include <stdint.h>
#include "soc/gpio_num.h"

typedef enum LEDEffect_t {
    LED_OFF = 0,
    LED_SOLID, //or current buffer
    LED_BLINK,
    LED_BREATHE,
    LED_CYCLIC,
    LED_RAINBOW,
    LED_RAW_BUFFER, //raw buffer effect - internal use only
} LEDEffect_t;

typedef struct LEDConfig_t {
    gpio_num_t pin;
    uint16_t count;
    bool is_rgbw;
} LEDConfig_t;

// LED persistent configuration structure
typedef struct {
    LEDEffect_t effect;
    uint8_t r, g, b, w;
    uint8_t brightness;
    uint8_t speed;
    bool on; // Track if LEDs are currently on
} led_persistent_config_t;

void led_set_effect(LEDEffect_t effect);
void led_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
void led_set_speed(uint8_t speed);
void led_set_brightness(uint8_t brightness);
void led_set_on_state(bool on);

uint8_t* led_get_buffer();
void led_fade_out();
void led_fade_in();
void led_init(LEDConfig_t config);
bool led_is_rgbw();

// LED mask functions
void led_set_mask(const uint8_t* mask);
void led_clear_mask();
const uint8_t* led_get_mask();

void led_load_from_nvs(led_persistent_config_t* config);
void led_save_to_nvs(led_persistent_config_t* config);
void led_apply_persistent_config(led_persistent_config_t* config);