#pragma once

#include <stdint.h>

// Include the configuration structure definition
#include "nixie_handlers.h"

void clock_init();
void nixie_set_brightness(uint8_t brightness);