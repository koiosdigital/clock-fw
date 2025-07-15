#pragma once

#include <stdint.h>
#include "driver/spi_master.h"

// SPI interface for Nixie tube shift register control
void nixie_spi_init(void);
void nixie_spi_transmit_bitstream(const uint8_t* bitstream, size_t length_bits);
void nixie_spi_deinit(void);
