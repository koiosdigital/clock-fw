#include "clock.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "led.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <internet_time.h>

#define NUM_PIXELS 6

gpio_num_t oe_pin = (gpio_num_t)11; // Example GPIO pin for output enable
gpio_num_t str_pin = (gpio_num_t)9; // Example GPIO pin for shift register clock

LEDConfig_t nixie_led_config = {
    .pin = (gpio_num_t)21,  // Example GPIO pin
    .count = NUM_PIXELS,
    .is_rgbw = true,   // Assuming RGB, not RGBW
};

#define SPI_HOST    SPI2_HOST
#define PIN_MOSI    10
#define PIN_SCLK    12

spi_device_handle_t spi_hv;

void spi_hv5222_init() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512, // 64 bits for two HV5222 drivers
    };
    esp_err_t err = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_DISABLED);
    assert(err == ESP_OK && "Failed to initialize SPI bus");

    // Configure SPI device interface without hardware CS (handled above)
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,         // No command phase
        .address_bits = 0,         // No address phase
        .dummy_bits = 0,           // No dummy bits
        .mode = 3,
        .clock_speed_hz = 200000, // 10 MHz
        .spics_io_num = -1,        // no hardware CS
        .queue_size = 2,
    };

    err = spi_bus_add_device(SPI_HOST, &devcfg, &spi_hv);
    assert(err == ESP_OK && "Failed to add device to SPI bus");
}

void update_hv_drivers(int h, int m, int s) {
    uint8_t bitstream[8];
    memset(bitstream, 0xFF, sizeof(bitstream));

    if ((s % 2) != 0) {
        for (int b = 0; b < 4; b++) {
            int bitPos = b;
            int byteIdx = bitPos / 8;
            int bitIdx = 7 - (bitPos % 8);
            bitstream[byteIdx] &= ~(1 << bitIdx);
        }
    }

    int groups[6] = { s % 10, s / 10, m % 10, m / 10, h % 10, h / 10 };
    int offsets[6] = { 4, 14, 24, 34, 44, 54 };
    for (int i = 0; i < 6; i++) {
        int digit = groups[i];
        int base = offsets[i];
        int bitInGroup = 9 - digit;
        int bitPos = base + bitInGroup;
        int byteIdx = bitPos / 8;
        int bitIdx = 7 - (bitPos % 8);
        bitstream[byteIdx] &= ~(1 << bitIdx);
    }

    spi_transaction_t txn = {};
    txn.tx_buffer = bitstream;
    txn.length = 64;

    esp_err_t err = spi_device_transmit(spi_hv, &txn);
    assert(err == ESP_OK && "Failed to transmit!");
}


void clock_task(void* pvParameters) {
    time_t now;
    struct tm timeinfo;
    int lastHour = -1;
    int lastMinute = -1;
    int lastSecond = -1;
    int lastCleaningDigit = -1;
    int cleaningIteration = 0;
    bool cleaning = false;

    led_set_color(255, 255, 0, 0);
    led_set_effect(LED_CYCLIC);
    led_set_brightness(255);
    update_hv_drivers(12, 34, 56);

    while (!is_time_synced()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    led_set_effect(LED_SOLID);
    led_set_brightness(255);
    led_set_color(255, 0, 0, 0);

    while (true) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (!cleaning) {
            if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute || timeinfo.tm_sec != lastSecond) {
                lastHour = timeinfo.tm_hour;
                lastMinute = timeinfo.tm_min;
                lastSecond = timeinfo.tm_sec;

                update_hv_drivers(lastHour, lastMinute, lastSecond);
            }

            if (lastHour == 4 && lastMinute == 0 && lastSecond == 0) { // Start cleaning at 4:00 AM local time
                cleaning = true;
            }
        }
        else {
            update_hv_drivers(lastCleaningDigit * 11, lastCleaningDigit * 11, lastCleaningDigit * 11);
            lastCleaningDigit = (lastCleaningDigit + 1) % 10;
            cleaningIteration++;
            if (cleaningIteration >= 3000) { //Clean for about 10 minutes
                cleaning = false;
                cleaningIteration = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void oe_pwm_init() {
    // Configure PWM via LEDC for output enable pin at 80% duty cycle
    ledc_timer_config_t pwm_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 40000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&pwm_timer);

    ledc_channel_config_t pwm_channel = {
        .gpio_num = oe_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = (uint32_t)(0.5 * ((1 << 8) - 1)), // 80% duty
        .hpoint = 0
    };
    ledc_channel_config(&pwm_channel);
}

void clock_init() {
    oe_pwm_init(); // Initialize PWM for output enable pin
    spi_hv5222_init();
    led_init(nixie_led_config);
    led_set_effect(LED_SOLID);
    led_set_color(255, 0, 0, 0); // Set color to red
    xTaskCreate(clock_task, "Clock Task", 8192, NULL, 5, NULL);
}