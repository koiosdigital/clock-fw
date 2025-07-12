#include "led.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#define LED_NVS_NAMESPACE "led_cfg"

// Default LED persistent configuration
static led_persistent_config_t led_persistent_config = {
    .effect = LED_SOLID,
    .r = 255, .g = 0, .b = 0, .w = 0,  // Default red
    .brightness = 255,
    .speed = 10
};

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
uint8_t* led_mask = nullptr;

void led_set_effect(LEDEffect_t effect) {
    current_effect = effect;
    led_persistent_config.effect = effect;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    led_color[0] = r;
    led_color[1] = g;
    led_color[2] = b;
    led_color[3] = w;
    led_persistent_config.r = r;
    led_persistent_config.g = g;
    led_persistent_config.b = b;
    led_persistent_config.w = w;
}

void led_set_speed(uint8_t speed) {
    led_speed = speed;
    led_persistent_config.speed = speed;
}

void led_set_brightness(uint8_t brightness) {
    led_brightness = brightness;
    led_persistent_config.brightness = brightness;
}

LEDEffect_t led_get_effect() {
    return current_effect;
}

void led_get_color(uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* w) {
    if (r) *r = led_color[0];
    if (g) *g = led_color[1];
    if (b) *b = led_color[2];
    if (w) *w = led_color[3];
}

uint8_t led_get_brightness() {
    return led_brightness;
}

uint8_t led_get_speed() {
    return led_speed;
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
        // Apply mask - only set color if mask bit is 1
        uint8_t masked_r = led_mask[i] ? r : 0;
        uint8_t masked_g = led_mask[i] ? g : 0;
        uint8_t masked_b = led_mask[i] ? b : 0;

        if (is_rgbw) {
            led_buffer[i * 4 + 0] = masked_g;
            led_buffer[i * 4 + 1] = masked_r;
            led_buffer[i * 4 + 2] = masked_b;
            led_buffer[i * 4 + 3] = 0;
        }
        else {
            led_buffer[i * 3 + 0] = masked_g;
            led_buffer[i * 3 + 1] = masked_r;
            led_buffer[i * 3 + 2] = masked_b;
        }
    }
}

void tx_buf_set_color_at(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= led_count) {
        return;
    }

    // Apply mask - only set color if mask bit is 1
    uint8_t masked_r = led_mask[index] ? r : 0;
    uint8_t masked_g = led_mask[index] ? g : 0;
    uint8_t masked_b = led_mask[index] ? b : 0;

    if (is_rgbw) {
        led_buffer[index * 4 + 0] = masked_g;
        led_buffer[index * 4 + 1] = masked_r;
        led_buffer[index * 4 + 2] = masked_b;
        led_buffer[index * 4 + 3] = 0;
    }
    else {
        led_buffer[index * 3 + 0] = masked_g;
        led_buffer[index * 3 + 1] = masked_r;
        led_buffer[index * 3 + 2] = masked_b;
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
        // Raw buffer effect - no processing, direct buffer access
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
    size_t bufSize = is_rgbw ? led_count * 4 : led_count * 3;
    led_buffer = (uint8_t*)malloc(bufSize);

    // Allocate and initialize mask buffer (defaults to all 1s)
    led_mask = (uint8_t*)malloc(led_count);
    memset(led_mask, 1, led_count);  // Default mask is all on

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
    // Load saved configuration from NVS
    led_load_config_from_nvs();

    // Apply loaded configuration
    current_effect = led_persistent_config.effect;
    led_color[0] = led_persistent_config.r;
    led_color[1] = led_persistent_config.g;
    led_color[2] = led_persistent_config.b;
    led_color[3] = led_persistent_config.w;
    led_brightness = led_persistent_config.brightness;
    led_speed = led_persistent_config.speed;


    LEDConfig_t* pConfig = (LEDConfig_t*)malloc(sizeof(LEDConfig_t));
    memcpy(pConfig, &led_config, sizeof(led_config));

    xTaskCreate(led_task, "led_task", 4096, pConfig, 5, NULL);
}

// NVS configuration functions
led_persistent_config_t led_get_persistent_config(void) {
    led_load_config_from_nvs();
    return led_persistent_config;
}

void led_set_persistent_config(const led_persistent_config_t* config) {
    if (config == NULL) return;

    led_persistent_config = *config;

    // Apply to current LED state
    current_effect = config->effect;
    led_color[0] = config->r;
    led_color[1] = config->g;
    led_color[2] = config->b;
    led_color[3] = config->w;
    led_brightness = config->brightness;
    led_speed = config->speed;

    led_save_config_to_nvs();
}

void led_load_config_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LED_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI("LED", "NVS namespace not found, using defaults");
        return;
    }

    size_t required_size = sizeof(led_persistent_config_t);
    err = nvs_get_blob(nvs_handle, "config", &led_persistent_config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI("LED", "Config not found in NVS, using defaults");
    }
    else {
        ESP_LOGI("LED", "Loaded config from NVS: effect=%d, color=(%d,%d,%d,%d), brightness=%d, speed=%d",
            led_persistent_config.effect, led_persistent_config.r, led_persistent_config.g,
            led_persistent_config.b, led_persistent_config.w, led_persistent_config.brightness,
            led_persistent_config.speed);
    }

    nvs_close(nvs_handle);
}

void led_save_config_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LED_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("LED", "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", &led_persistent_config, sizeof(led_persistent_config_t));
    if (err != ESP_OK) {
        ESP_LOGE("LED", "Failed to save config to NVS: %s", esp_err_to_name(err));
    }
    else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE("LED", "Failed to commit NVS: %s", esp_err_to_name(err));
        }
    }

    nvs_close(nvs_handle);
}

// LED mask functions
void led_set_mask(const uint8_t* mask) {
    if (mask == nullptr || led_mask == nullptr) {
        return;
    }
    memcpy(led_mask, mask, led_count);
}

void led_clear_mask() {
    if (led_mask == nullptr) {
        return;
    }
    memset(led_mask, 1, led_count);  // Reset to all on
}

const uint8_t* led_get_mask() {
    return led_mask;
}