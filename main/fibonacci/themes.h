#pragma once

#ifndef FIBONACCI_THEMES_H
#define FIBONACCI_THEMES_H

#include <stdint.h>

typedef struct fibonacci_colorTheme {
    uint8_t id;
    const char* name;
    uint32_t hour_color;
    uint32_t minute_color;
    uint32_t both_color;
} fibonacci_colorTheme;

//Colors represented in RGB. Off color is always white.
extern fibonacci_colorTheme colors[];

#endif

