#include "nixie.h"
#include "nixie_spi.h"
#include "nixie_oe.h"
#include "nixie_handlers.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "led.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include <internet_time.h>

#include <esp_event.h>

#include "sdkconfig.h"
#include <api.h>

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE

LEDConfig_t nixie_led_config = {
    .pin = (gpio_num_t)CONFIG_NIXIE_LED_DATA_PIN,
    .count = CONFIG_NIXIE_LED_COUNT,
    .is_rgbw = CONFIG_NIXIE_LED_IS_RGBW,
};

nixie_config_t nixie_config = {
    .brightness = 50,
    .military_time = false,
    .blinking_dots = true,
    .on = true, // Default to on
};

void nixie_show_time(int h, int m, int s) {
    if (!nixie_config.on) {
        uint8_t bitstream[8] = { 0 };
        nixie_spi_transmit_bitstream(bitstream, 64);
        return;
    }

    uint8_t bitstream[8] = { 0 };

    // Set blinking dots if enabled and seconds are odd
    if ((s % 2) != 0 && nixie_config.blinking_dots) {
        for (int b = 0; b < 4; b++) {
            int bitPos = b;
            int byteIdx = bitPos / 8;
            int bitIdx = 7 - (bitPos % 8);
            bitstream[byteIdx] |= (1 << bitIdx);
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
        bitstream[byteIdx] |= (1 << bitIdx);
    }

    nixie_spi_transmit_bitstream(bitstream, 64);
}


void nixie_clock_task(void* pvParameters) {
    time_t now;
    struct tm timeinfo;
    int lastHour = -1;
    int lastMinute = -1;
    int lastSecond = -1;
    int lastCleaningDigit = -1;
    int cleaningIteration = 0;
    bool cleaning = false;

    led_set_color(255, 255, 0, 0);  // Yellow during sync
    led_set_effect(LED_CYCLIC);
    led_set_brightness(255);

    while (!is_time_synced()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Restore saved LED configuration after sync
    led_persistent_config_t persistent_config;
    led_load_from_nvs(&persistent_config);
    led_apply_persistent_config(&persistent_config);

    while (true) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (!cleaning) {
            if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute || timeinfo.tm_sec != lastSecond) {
                lastHour = timeinfo.tm_hour;
                lastMinute = timeinfo.tm_min;
                lastSecond = timeinfo.tm_sec;

                int hour = lastHour;

                if (!nixie_config.military_time) {
                    if (hour >= 12) {
                        hour -= 12;
                        if (hour == 0) hour = 12; // Handle midnight case
                    }
                }

                nixie_show_time(hour, lastMinute, lastSecond);
            }

            if (lastHour == 4 && lastMinute == 0 && lastSecond == 0) { // Start cleaning at 4:00 AM local time
                cleaning = true;
            }
        }
        else {
            nixie_show_time(lastCleaningDigit * 11, lastCleaningDigit * 11, lastCleaningDigit * 11);
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

void nixie_clock_init() {
    httpd_handle_t server = get_httpd_handle();
    if (server) {
        register_nixie_handlers(server);
    }

    led_init(nixie_led_config);
    led_set_current_limit(600);

    led_set_effect(LED_BREATHE);
    led_set_color(0, 255, 255, 0);

    // Load nixie configuration from NVS
    nixie_load_from_nvs(&nixie_config);

    nixie_oe_init();
    nixie_spi_init();

    // Apply loaded configuration
    nixie_apply_config(&nixie_config);

    nixie_show_time(99, 99, 99);
}

void nixie_apply_config(nixie_config_t* config) {
    if (config == nullptr) return;

    nixie_config = *config;
    nixie_set_brightness(config->brightness);
}

#endif