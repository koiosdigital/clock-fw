#pragma once

#include "../def.h"

//Pins are the same as of latest rev, switch is kept for posterity reasons, and to allow future devices.
#if HW_TYPE == HW_TYPE_MOTHERSHIP
#define TX_PIN UART_PIN_NO_CHANGE
#define RX_PIN UART_PIN_NO_CHANGE
#define CTS_PIN 13
#define RTS_PIN 14
#elif HW_TYPE == HW_TYPE_ROVER
#define TX_PIN UART_PIN_NO_CHANGE
#define RX_PIN UART_PIN_NO_CHANGE
#define CTS_PIN 13
#define RTS_PIN 14
#else
#error "what are you doing?"
#endif

#define UART_NUM UART_NUM_0

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)

void uart_init();