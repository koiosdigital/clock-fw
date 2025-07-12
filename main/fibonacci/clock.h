#pragma once

#include "themes.h"

// Fibonacci configuration structure
typedef struct {
    uint8_t brightness;
    uint8_t theme_id;
} fibonacci_config_t;

void clock_init();

// Fibonacci configuration functions
void fibonacci_set_brightness(uint8_t brightness);
uint8_t fibonacci_get_brightness(void);
void fibonacci_set_theme(uint8_t theme_id);
uint8_t fibonacci_get_theme(void);
fibonacci_config_t fibonacci_get_config(void);
void fibonacci_set_config(const fibonacci_config_t* config);
uint8_t fibonacci_get_themes_count(void);
const fibonacci_colorTheme* fibonacci_get_theme_info(uint8_t theme_id);

// NVS functions
void fibonacci_load_config_from_nvs(void);
void fibonacci_save_config_to_nvs(void);
