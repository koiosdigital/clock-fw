#pragma once

#include <stdint.h>
#include "driver/gpio.h"

/**
 * @brief Initialize PWM for Output Enable (OE) pin control
 *
 * Configures LEDC timer and channel for brightness control via PWM
 * on the nixie tube output enable pin.
 */
void nixie_oe_init(void);

/**
 * @brief Set nixie tube brightness
 *
 * @param brightness_percent Brightness level from 0-100%
 *                          Maps to 20-100% duty cycle (OE is active low)
 */
void nixie_set_brightness(uint8_t brightness_percent);

/**
 * @brief Get the configured OE pin
 *
 * @return gpio_num_t The GPIO pin number for Output Enable
 */
gpio_num_t nixie_oe_get_pin(void);
