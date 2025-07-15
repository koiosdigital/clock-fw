#pragma once

#include "themes.h"
#include <stdint.h>

typedef struct {
    uint8_t brightness;      // 0-255
    uint8_t theme_id;       // Index of the current theme
    bool on;                // true if Fibonacci clock is on, false if off
} fibonacci_config_t;

// Fibonacci configuration functions (simplified - no getters)
void fibonacci_set_brightness(uint8_t brightness);
void fibonacci_set_theme(uint8_t theme_id);
void fibonacci_set_on_state(bool on);

void fibonacci_clock_init();
void fibonacci_clock_task(void* pvParameters);

// Theme functions (these remain as they're utility functions)
uint8_t fibonacci_get_themes_count(void);
const fibonacci_colorTheme* fibonacci_get_theme_info(uint8_t theme_id);
