#pragma once

#include "themes.h"
#include <stdint.h>

// Include the configuration structure definition
#include "fibonacci_handlers.h"

// Fibonacci configuration functions (simplified - no getters)
void fibonacci_set_brightness(uint8_t brightness);
void fibonacci_set_theme(uint8_t theme_id);
void fibonacci_set_on_state(bool on);

void clock_init();

// Theme functions (these remain as they're utility functions)
uint8_t fibonacci_get_themes_count(void);
const fibonacci_colorTheme* fibonacci_get_theme_info(uint8_t theme_id);
