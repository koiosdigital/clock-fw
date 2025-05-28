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
    LED_RAW_BUFFER, //raw buffer effect
} LEDEffect_t;

typedef struct LEDConfig_t {
    gpio_num_t pin;
    uint16_t count;
    bool is_rgbw;
} LEDConfig_t;

void led_set_effect(LEDEffect_t effect);
void led_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
void led_set_speed(uint8_t speed);
uint8_t* led_get_buffer();
void led_set_brightness(uint8_t brightness);
void led_fade_out();
void led_fade_in();
void led_init(LEDConfig_t config);