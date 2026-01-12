#include "nixie.h"
#include "nixie_spi.h"
#include "nixie_oe.h"
#include "nixie_handlers.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "kd_pixdriver.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include "kd_common.h"
#include "clock_events.h"

#include <esp_event.h>

#include "sdkconfig.h"
#include <api.h>

static const char* TAG = "nixie";

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE

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


// Shared state for the nixie clock task
static volatile bool g_ntp_synced = false;
static volatile bool g_cleaning = false;
static volatile int g_cleaning_digit = 0;
static volatile int g_cleaning_iteration = 0;

// Update the display with current time
static void update_display(void) {
    if (!g_ntp_synced) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    if (!nixie_config.military_time) {
        if (hour >= 12) {
            hour -= 12;
            if (hour == 0) hour = 12;
        }
    }

    ESP_LOGD(TAG, "Updating display: %02d:%02d:%02d", hour, timeinfo.tm_min, timeinfo.tm_sec);
    nixie_show_time(hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Event handler for clock events
static void clock_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == CLOCK_EVENTS) {
        switch (id) {
            case CLOCK_EVENT_SECOND_TICK:
                // Update display every second
                if (!g_cleaning) {
                    update_display();
                }
                break;
            case CLOCK_EVENT_HOUR_TICK: {
                // Check for cleaning trigger at 4:00 AM
                clock_time_event_data_t* time_data = (clock_time_event_data_t*)data;
                if (time_data && time_data->hour == 4 && time_data->minute == 0) {
                    ESP_LOGI(TAG, "Starting cathode cleaning cycle");
                    g_cleaning = true;
                    g_cleaning_digit = 0;
                    g_cleaning_iteration = 0;
                }
                break;
            }
            case CLOCK_EVENT_CONFIG_CHANGED:
            case CLOCK_EVENT_FORCE_REFRESH:
                // Force immediate display update
                if (!g_cleaning) {
                    update_display();
                }
                break;
            default:
                break;
        }
    }
}

// Event handler for NTP sync
static void ntp_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == KD_NTP_EVENT_SYNC_COMPLETE) {
        ESP_LOGI(TAG, "NTP synced, loading display settings");
        g_ntp_synced = true;
        PixelDriver::getMainChannel()->loadFromNVS();
        update_display();
    }
    else if (id == KD_NTP_EVENT_SYNC_LOST) {
        g_ntp_synced = false;
    }
}

void nixie_clock_task(void* pvParameters) {
    ESP_LOGI(TAG, "Nixie clock task started");

    // Show syncing animation
    PixelDriver::getMainChannel()->setColor(PixelColor(255, 255, 0)); // Yellow during sync
    PixelDriver::getMainChannel()->setEffectByID("CYCLIC");
    PixelDriver::getMainChannel()->setBrightness(255);

    // Register for events
    esp_event_handler_register(CLOCK_EVENTS, ESP_EVENT_ANY_ID, clock_event_handler, nullptr);
    esp_event_handler_register(KD_NTP_EVENTS, ESP_EVENT_ANY_ID, ntp_event_handler, nullptr);

    // If already synced, switch to display mode immediately
    if (kd_common_ntp_is_synced()) {
        g_ntp_synced = true;
        PixelDriver::getMainChannel()->loadFromNVS();
    }

    // Polling loop only needed for cleaning cycle animation
    // Normal time updates are handled by CLOCK_EVENT_SECOND_TICK
    while (true) {
        if (g_ntp_synced && g_cleaning) {
            // Cleaning mode - cycle through all digits
            nixie_show_time(g_cleaning_digit * 11, g_cleaning_digit * 11, g_cleaning_digit * 11);
            g_cleaning_digit = (g_cleaning_digit + 1) % 10;
            g_cleaning_iteration++;
            if (g_cleaning_iteration >= 3000) {  // ~10 minutes at 200ms
                ESP_LOGI(TAG, "Cathode cleaning complete");
                g_cleaning = false;
                g_cleaning_iteration = 0;
                update_display();
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

    PixelDriver::initialize(60);
    PixelDriver::addChannel(ChannelConfig((gpio_num_t)CONFIG_NIXIE_LED_DATA_PIN, CONFIG_NIXIE_LED_COUNT, CONFIG_NIXIE_LED_IS_RGBW ? PixelFormat::RGBW : PixelFormat::RGB, "Backlight"));
    PixelDriver::setCurrentLimit(600); // 600mA limit for Nixie LEDs
    PixelDriver::start();

    PixelDriver::getMainChannel()->setColor(PixelColor(0, 255, 255));
    PixelDriver::getMainChannel()->setEffectByID("BREATHE");

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