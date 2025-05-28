#pragma once

#include <stdint.h>

typedef struct fibonacci_colorTheme {
    uint8_t id;
    const char* name;
    uint32_t hour_color;
    uint32_t minute_color;
    uint32_t both_color;
} fibonacci_colorTheme;

//Colors represented in RGB. Off color is always white.
fibonacci_colorTheme colors[] = {
    { 0, "RGB",     0xFF0A0A, 0x0AFF0A, 0x0A0AFF },
    { 1, "Mondrian",0xFF0A0A, 0xF8DE00, 0x0A0AFF },
    { 2, "Basbrun", 0x502800, 0x14C814, 0xFF640A },
    { 3, "80's",    0xF564C9, 0x72F736, 0x71EBDC },
    { 4, "Pastel",  0xFF7B7B, 0x8FFF70, 0x7878FF },
    { 5, "Modern",  0xD4312D, 0x91D231, 0x8D5FE0 },
    { 6, "Cold",    0xD13EC8, 0x45E8E0, 0x5046CA },
    { 7, "Warm",    0xED1414, 0xF6F336, 0xFF7E15 },
    { 8, "Earth",   0x462300, 0x467A0A, 0xC8B600 },
    { 9, "Dark",    0xD32222, 0x50974E, 0x101895 }
};

