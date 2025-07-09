#include "clock.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "led.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <ctime>
#include "themes.h"

#define NUM_PIXELS 9

LEDConfig_t fibonacci_led_config = {
    .pin = (gpio_num_t)14,  // Example GPIO pin
    .count = NUM_PIXELS,
    .is_rgbw = false,   // Assuming RGB, not RGBW
};

uint8_t* pixel_buffer = nullptr;
uint8_t bits[NUM_PIXELS] = { 0 };
bool on = true;

int random(int max)
{
    return esp_random() % max;
}

void setPixelHelper(uint8_t pixel, uint32_t color)
{
    pixel_buffer[pixel * 3] = (color >> 16) & 0xFF; // Red
    pixel_buffer[pixel * 3 + 1] = (color >> 8) & 0xFF; // Green
    pixel_buffer[pixel * 3 + 2] = color & 0xFF; // Blue
    ESP_LOGI("clock", "Set pixel %d to color %02X %02X %02X", pixel,
        pixel_buffer[pixel * 3], pixel_buffer[pixel * 3 + 1], pixel_buffer[pixel * 3 + 2]);
}

void setPixel(uint8_t pixel, uint32_t color)
{
    if (!on)
        return;

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
    for (int i = 0; i < NUM_PIXELS; i++)
        bits[i] = 0;

    setBits(hours, 0x01);
    setBits(minutes / 5, 0x02);

    for (int i = 0; i < NUM_PIXELS; i++)
    {
        switch (bits[i]) {
        case 1:
            setPixel(i, colors[0].hour_color);
            break;
        case 2:
            setPixel(i, colors[0].minute_color);
            break;
        case 3:
            setPixel(i, colors[0].both_color);
            break;
        default:
            setPixel(i, 0xFFFFFF); // Off color (white)
            break;
        }
    }
}

void clock_task(void* pvParameters) {
    time_t now;
    struct tm timeinfo;
    int lastHour = -1;
    int lastMinute = -1;

    while (true) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute) {
            lastHour = timeinfo.tm_hour;
            lastMinute = timeinfo.tm_min;

            // Set the pixels based on the current time
            setTime(lastHour, lastMinute);
        }


        // Simulate a delay for the clock update
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void clock_init() {
    led_init(fibonacci_led_config);
    led_set_effect(LED_RAW_BUFFER);
    pixel_buffer = led_get_buffer();

    if (pixel_buffer == nullptr) {
        ESP_LOGE("Clock", "Failed to get pixel buffer");
        return;
    }

    xTaskCreate(clock_task, "Clock Task", 4096, NULL, 5, NULL);
}