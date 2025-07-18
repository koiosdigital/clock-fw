#include "fibonacci.h"
#include "fibonacci_handlers.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "led.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <ctime>
#include <cstring>
#include "themes.h"

#include <internet_time.h>

#include "sdkconfig.h"
#include <api.h>

#ifdef CONFIG_BASE_CLOCK_TYPE_FIBONACCI

LEDConfig_t fibonacci_led_config = {
    .pin = (gpio_num_t)4,  // Example GPIO pin
    .count = 9,
    .is_rgbw = false,   // Assuming RGB, not RGBW
};

uint8_t* pixel_buffer = nullptr;
uint8_t bits[9] = { 0 };

static fibonacci_config_t fib_config = {
    .brightness = 255,  // Default full brightness
    .theme_id = 0,      // Default to RGB theme
    .on = true          // Default on
};

#define FIBONACCI_THEMES_COUNT (sizeof(colors) / sizeof(colors[0]))

// Define the colors array (declared as extern in themes.h)
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

int random(int max)
{
    return esp_random() % max;
}

void setPixelHelper(uint8_t pixel, uint32_t color)
{
    pixel_buffer[pixel * 3] = (color >> 16) & 0xFF; // Red
    pixel_buffer[pixel * 3 + 1] = (color >> 8) & 0xFF; // Green
    pixel_buffer[pixel * 3 + 2] = color & 0xFF; // Blue
}

void setPixel(uint8_t pixel, uint32_t color)
{
    if (!fib_config.on) {
        // If the clock is off, clear the pixel
        setPixelHelper(pixel, 0);
        return;
    }

    switch (pixel)
    {
    case 0:
        setPixelHelper(0, color);
        break;
    case 1:
        setPixelHelper(1, color);
        break;
    case 2:
        setPixelHelper(2, color);
        break;
    case 3:
        setPixelHelper(3, color);
        setPixelHelper(4, color);
        break;
    case 4:
        setPixelHelper(5, color);
        setPixelHelper(6, color);
        setPixelHelper(7, color);
        setPixelHelper(8, color);
        break;
    };
}

void setBits(uint8_t value, uint8_t offset)
{
    switch (value)
    {
    case 1:
        switch (random(2))
        {
        case 0:
            bits[0] |= offset;
            break;
        case 1:
            bits[1] |= offset;
            break;
        }
        break;
    case 2:
        switch (random(2))
        {
        case 0:
            bits[2] |= offset;
            break;
        case 1:
            bits[0] |= offset;
            bits[1] |= offset;
            break;
        }
        break;
    case 3:
        switch (random(3))
        {
        case 0:
            bits[3] |= offset;
            break;
        case 1:
            bits[0] |= offset;
            bits[2] |= offset;
            break;
        case 2:
            bits[1] |= offset;
            bits[2] |= offset;
            break;
        }
        break;
    case 4:
        switch (random(3))
        {
        case 0:
            bits[0] |= offset;
            bits[3] |= offset;
            break;
        case 1:
            bits[1] |= offset;
            bits[3] |= offset;
            break;
        case 2:
            bits[0] |= offset;
            bits[1] |= offset;
            bits[2] |= offset;
            break;
        }
        break;
    case 5:
        switch (random(3))
        {
        case 0:
            bits[4] |= offset;
            break;
        case 1:
            bits[2] |= offset;
            bits[3] |= offset;
            break;
        case 2:
            bits[0] |= offset;
            bits[1] |= offset;
            bits[3] |= offset;
            break;
        }
        break;
    case 6:
        switch (random(4))
        {
        case 0:
            bits[0] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[1] |= offset;
            bits[4] |= offset;
            break;
        case 2:
            bits[0] |= offset;
            bits[2] |= offset;
            bits[3] |= offset;
            break;
        case 3:
            bits[1] |= offset;
            bits[2] |= offset;
            bits[3] |= offset;
            break;
        }
        break;
    case 7:
        switch (random(3))
        {
        case 0:
            bits[2] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[0] |= offset;
            bits[1] |= offset;
            bits[4] |= offset;
            break;
        case 2:
            bits[0] |= offset;
            bits[1] |= offset;
            bits[2] |= offset;
            bits[3] |= offset;
            break;
        }
        break;
    case 8:
        switch (random(3))
        {
        case 0:
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[0] |= offset;
            bits[2] |= offset;
            bits[4] |= offset;
            break;
        case 2:
            bits[1] |= offset;
            bits[2] |= offset;
            bits[4] |= offset;
            break;
        }
        break;
    case 9:
        switch (random(2))
        {
        case 0:
            bits[0] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[1] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        }
        break;
    case 10:
        switch (random(2))
        {
        case 0:
            bits[2] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[0] |= offset;
            bits[1] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        }
        break;
    case 11:
        switch (random(2))
        {
        case 0:
            bits[0] |= offset;
            bits[2] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        case 1:
            bits[1] |= offset;
            bits[2] |= offset;
            bits[3] |= offset;
            bits[4] |= offset;
            break;
        }

        break;
    case 12:
        bits[0] |= offset;
        bits[1] |= offset;
        bits[2] |= offset;
        bits[3] |= offset;
        bits[4] |= offset;

        break;
    }
}

void setTime(uint8_t hours, uint8_t minutes)
{
    // Clear all pixels first
    memset(pixel_buffer, 0, 9 * 3);

    for (int i = 0; i < 9; i++)
        bits[i] = 0;

    setBits(hours, 0x01);
    setBits(minutes / 5, 0x02);

    for (int i = 0; i < 9; i++)
    {
        switch (bits[i]) {
        case 1:
            setPixel(i, colors[fib_config.theme_id].hour_color);
            break;
        case 2:
            setPixel(i, colors[fib_config.theme_id].minute_color);
            break;
        case 3:
            setPixel(i, colors[fib_config.theme_id].both_color);
            break;
        default:
            setPixel(i, 0xFFFFFF); // Off color (white)
            break;
        }
    }
}

bool forceUpdate = false;
void fibonacci_clock_task(void* pvParameters) {
    time_t now;
    struct tm timeinfo;
    int lastHour = -1;
    int lastMinute = -1;

    led_set_color(255, 255, 0, 0);  // Yellow during sync
    led_set_effect(LED_CYCLIC);
    led_set_brightness(255);

    while (!is_time_synced()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    led_set_effect(LED_RAW_BUFFER);

    while (true) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute || forceUpdate) {
            lastHour = timeinfo.tm_hour;
            lastMinute = timeinfo.tm_min;
            forceUpdate = false; // Reset force update flag after processing

            // Set the pixels based on the current time
            setTime(lastHour % 12, lastMinute);
        }


        // Simulate a delay for the clock update
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Fibonacci configuration functions
void fibonacci_set_brightness(uint8_t brightness) {
    fib_config.brightness = brightness;
    led_set_brightness(brightness);
    fibonacci_save_to_nvs(&fib_config);
    forceUpdate = true; // Force update the display
}

void fibonacci_set_theme(uint8_t theme_id) {
    if (theme_id < FIBONACCI_THEMES_COUNT) {
        fib_config.theme_id = theme_id;
        fibonacci_save_to_nvs(&fib_config);
    }
    forceUpdate = true; // Force update the display
}

void fibonacci_set_on_state(bool on) {
    fib_config.on = on;
    fibonacci_save_to_nvs(&fib_config);
    forceUpdate = true; // Force update the display
}

void fibonacci_apply_config(fibonacci_config_t* config) {
    if (config == NULL) return;

    fibonacci_set_brightness(config->brightness);
    fibonacci_set_theme(config->theme_id);
    fibonacci_set_on_state(config->on);
}

uint8_t fibonacci_get_themes_count(void) {
    return FIBONACCI_THEMES_COUNT;
}

const fibonacci_colorTheme* fibonacci_get_theme_info(uint8_t theme_id) {
    if (theme_id < FIBONACCI_THEMES_COUNT) {
        return &colors[theme_id];
    }
    return NULL;
}


void fibonacci_clock_init() {
    httpd_handle_t server = get_httpd_handle();
    if (server) {
        register_fibonacci_handlers(server);
    }

    led_init(fibonacci_led_config);
    led_set_current_limit(600);

    pixel_buffer = led_get_buffer();

    if (pixel_buffer == nullptr) {
        esp_restart(); // Critical failure, cannot proceed without pixel buffer
        return;
    }

    // Load fibonacci configuration from NVS
    fibonacci_load_from_nvs(&fib_config);

    // Apply loaded configuration
    fibonacci_apply_config(&fib_config);

    led_set_effect(LED_BREATHE);  // Start with blinking teal waiting for WiFi
    led_set_color(0, 255, 255, 0);  // Teal color
}
#endif