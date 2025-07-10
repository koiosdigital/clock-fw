#include "led.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

static const char* TAG = "led";

LEDEffect_t current_effect = LED_OFF;
static uint8_t led_speed = 10;
static uint8_t led_brightness = 255;
static uint8_t led_color[4] = { 0, 0, 0, 0 };
static bool fading_out = false;
static uint32_t blink_changed_at = 0;
static bool blink_state = false;
static bool fading_in = false;
static uint16_t led_count = 0;
static bool is_rgbw = false;

uint8_t* led_buffer = nullptr;

void led_set_effect(LEDEffect_t effect) {
    current_effect = effect;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    led_color[0] = r;
    led_color[1] = g;
    led_color[2] = b;
    led_color[3] = w;
}

void led_set_speed(uint8_t speed) {
    led_speed = speed;
}

void led_set_brightness(uint8_t brightness) {
    led_brightness = brightness;
}

void led_fade_out() {
    fading_out = true;
    fading_in = false;
}

void led_fade_in() {
    fading_in = true;
    fading_out = false;
}

void handle_fading() {
    if (fading_out) {
        led_brightness -= 5;
        if (led_brightness <= 0) {
            led_brightness = 0;
            fading_out = false;
        }
    }
    else if (fading_in) {
        led_brightness += 5;
        if (led_brightness >= 255) {
            led_brightness = 255;
            fading_in = false;
        }
    }
}

void tx_buf_fill_color(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < led_count; i++) {
        if (is_rgbw) {
            led_buffer[i * 4 + 0] = g;
            led_buffer[i * 4 + 1] = r;
            led_buffer[i * 4 + 2] = b;
            led_buffer[i * 4 + 3] = 0;
        }
        else {
            led_buffer[i * 3 + 0] = g;
            led_buffer[i * 3 + 1] = r;
            led_buffer[i * 3 + 2] = b;
        }
    }
}

void tx_buf_set_color_at(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= led_count) {
        ESP_LOGE(TAG, "Index out of bounds");
        return;
    }
    if (is_rgbw) {
        led_buffer[index * 4 + 0] = g;
        led_buffer[index * 4 + 1] = r;
        led_buffer[index * 4 + 2] = b;
        led_buffer[index * 4 + 3] = 0;
    }
    else {
        led_buffer[index * 3 + 0] = g;
        led_buffer[index * 3 + 1] = r;
        led_buffer[index * 3 + 2] = b;
    }

}

void led_blink() {
    uint32_t changeInterval = 1000 / led_speed;
    if (xTaskGetTickCount() - blink_changed_at > pdMS_TO_TICKS(changeInterval)) {
        blink_state = !blink_state;
        if (blink_state) {
            tx_buf_fill_color(led_color[0], led_color[1], led_color[2]);
        }
        else {
            tx_buf_fill_color(0, 0, 0);
        }
        blink_changed_at = xTaskGetTickCount();
    }
}

void led_breathe() {
    static uint8_t brightness = 0;
    static bool increasing = true;

    if (increasing) {
        brightness += 5;
        if (brightness >= 255) {
            brightness = 255;
            increasing = false;
        }
    }
    else {
        brightness -= 5;
        if (brightness <= 0) {
            brightness = 0;
            increasing = true;
        }
    }

    tx_buf_fill_color(led_color[0] * brightness / 255, led_color[1] * brightness / 255, led_color[2] * brightness / 255);
}

//LEDs are arranged in a circle, loading spinner effect
void led_cyclic() {
    static uint8_t offset = 0;
    static uint32_t last_update = 0;
    static int trail_size = 5;

    uint32_t changeInterval = 1000 / led_speed;
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        offset = (offset + 1) % led_count;
        for (int i = 0; i < led_count; i++) {
            if (i < trail_size) {
                tx_buf_set_color_at((i + offset) % led_count, led_color[0], led_color[1], led_color[2]);
            }
            else {
                tx_buf_set_color_at((i + offset) % led_count, 0, 0, 0);
            }
        }
        last_update = xTaskGetTickCount();
    }
}

void led_loop() {
    handle_fading();

    switch (current_effect) {
    case LED_OFF:
        tx_buf_fill_color(0, 0, 0);
        break;
    case LED_SOLID:
        tx_buf_fill_color(led_color[0] * led_brightness / 255, led_color[1] * led_brightness / 255, led_color[2] * led_brightness / 255);
        break;
    case LED_BLINK:
        led_blink();
        break;
    case LED_BREATHE:
        led_breathe();
        break;
    case LED_CYCLIC:
        led_cyclic();
        break;
    case LED_RAINBOW:
        // Implement rainbow effect here
        break;
    case LED_RAW_BUFFER:
        // Implement raw buffer effect here
        break;
    }
}

uint8_t* led_get_buffer() {
    return led_buffer;
}

void led_task(void* pvParameter) {
    LEDConfig_t* config = (LEDConfig_t*)pvParameter;

    led_count = config->count;
    is_rgbw = config->is_rgbw;
    size_t bufSize = config->is_rgbw ? led_count * 4 : led_count * 3;
    led_buffer = (uint8_t*)malloc(bufSize);

    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)config->pin,
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .resolution_hz = 10000000,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };

    rmt_new_tx_channel(&tx_chan_config, &led_chan);

    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = 10000000,
    };
    rmt_new_led_strip_encoder(&encoder_config, &led_encoder);
    rmt_enable(led_chan);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };

    while (1) {
        led_loop();

        rmt_transmit(led_chan, led_encoder, led_buffer, bufSize, &tx_config);
        rmt_tx_wait_all_done(led_chan, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(1000 / 15));
    }
}

void led_init(LEDConfig_t led_config)
{
    LEDConfig_t* pConfig = (LEDConfig_t*)malloc(sizeof(LEDConfig_t));
    memcpy(pConfig, &led_config, sizeof(led_config));

    xTaskCreate(led_task, "led_task", 4096, pConfig, 5, NULL);

    led_set_effect(LED_SOLID);
    led_set_color(255, 255, 255, 255);
    led_set_brightness(255);
    led_fade_out();
}