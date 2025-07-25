#include "led.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "esp_random.h"

#define LED_NVS_NAMESPACE "led_cfg"

static uint16_t led_count = 0;
static bool is_rgbw = false;

LEDEffect_t current_effect = LED_SOLID;
static uint8_t led_speed = 10;
static uint8_t led_brightness = 255;
static uint8_t led_color[4] = { 0, 0, 0, 0 };
static bool leds_on = false;

// Current limiting variables
static int32_t current_limit_ma = -1;  // -1 = unlimited, otherwise limit in mA
static uint8_t* led_scaled_buffer = nullptr;  // Secondary buffer for current-limited output

static bool fading_out = false;
static bool fading_in = false;

static uint32_t blink_changed_at = 0;
static bool blink_state = false;

uint8_t* led_buffer = nullptr;
uint8_t* led_mask = nullptr;

void led_apply_current_limiting(); // Forward declaration

// HSV to RGB conversion helper function
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        *r = v;
        *g = v;
        *b = v;
        return;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
    case 0:
        *r = v; *g = t; *b = p;
        break;
    case 1:
        *r = q; *g = v; *b = p;
        break;
    case 2:
        *r = p; *g = v; *b = t;
        break;
    case 3:
        *r = p; *g = q; *b = v;
        break;
    case 4:
        *r = t; *g = p; *b = v;
        break;
    default:
        *r = v; *g = p; *b = q;
        break;
    }
}

//Setters
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

void led_set_on_state(bool on) {
    leds_on = on;
}

// Current limiting functions
void led_set_current_limit(int32_t limit_ma) {
    current_limit_ma = limit_ma;
}

int32_t led_get_current_limit(void) {
    return current_limit_ma;
}

//Helpers
void led_fade_out() {
    fading_out = true;
    fading_in = false;
}

void led_fade_in() {
    fading_in = true;
    fading_out = false;
}

bool led_is_rgbw() {
    return is_rgbw;
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

void tx_buf_fill_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    for (int i = 0; i < led_count; i++) {
        // Apply mask - only set color if mask bit is 1
        uint8_t masked_r = led_mask[i] ? r : 0;
        uint8_t masked_g = led_mask[i] ? g : 0;
        uint8_t masked_b = led_mask[i] ? b : 0;
        uint8_t masked_w = led_mask[i] ? w : 0;

        if (is_rgbw) {
            led_buffer[i * 4 + 0] = masked_g;
            led_buffer[i * 4 + 1] = masked_r;
            led_buffer[i * 4 + 2] = masked_b;
            led_buffer[i * 4 + 3] = masked_w;
        }
        else {
            led_buffer[i * 3 + 0] = masked_g;
            led_buffer[i * 3 + 1] = masked_r;
            led_buffer[i * 3 + 2] = masked_b;
        }
    }
}

void tx_buf_set_color_at(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index < 0 || index >= led_count) {
        return;
    }

    // Apply mask - only set color if mask bit is 1
    uint8_t masked_r = led_mask[index] ? r : 0;
    uint8_t masked_g = led_mask[index] ? g : 0;
    uint8_t masked_b = led_mask[index] ? b : 0;
    uint8_t masked_w = led_mask[index] ? w : 0;

    if (is_rgbw) {
        led_buffer[index * 4 + 0] = masked_g;
        led_buffer[index * 4 + 1] = masked_r;
        led_buffer[index * 4 + 2] = masked_b;
        led_buffer[index * 4 + 3] = masked_w;
    }
    else {
        led_buffer[index * 3 + 0] = masked_g;
        led_buffer[index * 3 + 1] = masked_r;
        led_buffer[index * 3 + 2] = masked_b;
    }

}

void led_blink() {
    uint32_t changeInterval = 1000 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - blink_changed_at > pdMS_TO_TICKS(changeInterval)) {
        blink_state = !blink_state;
        if (blink_state) {
            tx_buf_fill_color(led_color[0], led_color[1], led_color[2], led_color[3]);
        }
        else {
            tx_buf_fill_color(0, 0, 0, 0);
        }
        blink_changed_at = xTaskGetTickCount();
    }
}

void led_breathe() {
    static uint8_t brightness = 0;
    static bool increasing = true;

    uint32_t changeInterval = 100 * (11 - led_speed); // Inverse speed logic
    static uint32_t last_update = 0;

    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        if (increasing) {
            brightness += 5;
            if (brightness >= led_brightness) {
                brightness = led_brightness;
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

        tx_buf_fill_color(led_color[0] * brightness / 255, led_color[1] * brightness / 255, led_color[2] * brightness / 255, led_color[3] * brightness / 255);
        last_update = xTaskGetTickCount();
    }
}

//LEDs are arranged in a circle, loading spinner effect
void led_cyclic() {
    static uint8_t offset = 0;
    static uint32_t last_update = 0;
    static int trail_size = 5;

    uint32_t changeInterval = 100 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        offset = (offset + 1) % led_count;
        for (int i = 0; i < led_count; i++) {
            if (i < trail_size) {
                tx_buf_set_color_at((i + offset) % led_count, led_color[0], led_color[1], led_color[2], led_color[3]);
            }
            else {
                tx_buf_set_color_at((i + offset) % led_count, 0, 0, 0, 0);
            }
        }
        last_update = xTaskGetTickCount();
    }
}

void led_rainbow() {
    static uint8_t rainbow_offset = 0;
    static uint32_t last_update = 0;

    uint32_t changeInterval = 100 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        rainbow_offset = (rainbow_offset + 1) % 256;
        last_update = xTaskGetTickCount();
    }

    for (int i = 0; i < led_count; i++) {
        // Calculate hue for this LED position with offset for animation
        uint8_t hue = (uint8_t)((i * 256 / led_count) + rainbow_offset) % 256;

        // Convert HSV to RGB (H=hue, S=255, V=brightness)
        uint8_t r, g, b;
        hsv_to_rgb(hue, 255, led_brightness, &r, &g, &b);

        // Set color (RGB only, no white channel for rainbow)
        tx_buf_set_color_at(i, r, g, b, 0);
    }
}

void led_color_wipe() {
    static int current_led = 0;
    static uint32_t last_update = 0;

    uint32_t changeInterval = 100 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        if (current_led < led_count) {
            tx_buf_set_color_at(current_led, led_color[0], led_color[1], led_color[2], led_color[3]);
            current_led++;
        }
        else {
            current_led = 0;
            tx_buf_fill_color(0, 0, 0, 0); // Clear all LEDs before restarting
        }
        last_update = xTaskGetTickCount();
    }
}

void led_theater_chase() {
    static int offset = 0;
    static uint32_t last_update = 0;

    uint32_t changeInterval = 100 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        for (int i = 0; i < led_count; i++) {
            if ((i + offset) % 3 == 0) {
                tx_buf_set_color_at(i, led_color[0], led_color[1], led_color[2], led_color[3]);
            }
            else {
                tx_buf_set_color_at(i, 0, 0, 0, 0);
            }
        }
        offset = (offset + 1) % 3;
        last_update = xTaskGetTickCount();
    }
}

void led_sparkle() {
    static uint32_t last_update = 0;

    uint32_t changeInterval = 50 * (11 - led_speed); // Inverse speed logic
    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(changeInterval)) {
        for (int i = 0; i < led_count; i++) {
            if (esp_random() % 10 == 0) { // Randomly sparkle some LEDs
                tx_buf_set_color_at(i, led_color[0], led_color[1], led_color[2], led_color[3]);
            }
            else {
                tx_buf_set_color_at(i, 0, 0, 0, 0);
            }
        }
        last_update = xTaskGetTickCount();
    }
}

void led_loop() {
    handle_fading();

    if (!leds_on) {
        tx_buf_fill_color(0, 0, 0, 0);
        return;
    }

    switch (current_effect) {
    case LED_SOLID:
        tx_buf_fill_color(led_color[0] * led_brightness / 255, led_color[1] * led_brightness / 255, led_color[2] * led_brightness / 255, led_color[3] * led_brightness / 255);
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
        led_rainbow();
        break;
    case LED_COLOR_WIPE:
        led_color_wipe();
        break;
    case LED_THEATER_CHASE:
        led_theater_chase();
        break;
    case LED_SPARKLE:
        led_sparkle();
        break;
    default:
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
    led_scaled_buffer = (uint8_t*)malloc(bufSize);  // Allocate secondary buffer

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

        // Apply current limiting to secondary buffer
        led_apply_current_limiting();

        rmt_transmit(led_chan, led_encoder, led_scaled_buffer, bufSize, &tx_config);
        rmt_tx_wait_all_done(led_chan, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(1000 / 15));
    }
}

void led_init(LEDConfig_t led_config)
{
    LEDConfig_t* pConfig = (LEDConfig_t*)malloc(sizeof(LEDConfig_t));
    memcpy(pConfig, &led_config, sizeof(led_config));

    xTaskCreate(led_task, "led_task", 4096, pConfig, 5, NULL);

    led_persistent_config_t persistent_config;
    led_load_from_nvs(&persistent_config);
    led_apply_persistent_config(&persistent_config);
}

void led_apply_persistent_config(led_persistent_config_t* config) {
    if (config == nullptr) {
        ESP_LOGE("LED", "Invalid persistent config pointer");
        return;
    }

    // Apply loaded or default configuration
    led_set_effect(config->effect);
    led_set_color(config->r, config->g, config->b, config->w);
    led_set_brightness(config->brightness);
    led_set_speed(config->speed);
    led_set_on_state(config->on);
}

void led_load_from_nvs(led_persistent_config_t* config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LED_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI("LED", "NVS namespace not found, using defaults");
        config->effect = LED_SOLID;
        config->r = 100;
        config->g = 100;
        config->b = 100;
        config->w = 100;
        config->brightness = 100;
        config->speed = 10;
        config->on = true;
        return;
    }

    size_t required_size = sizeof(led_persistent_config_t);
    err = nvs_get_blob(nvs_handle, "config", config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI("LED", "Config not found in NVS, using defaults");
        config->effect = LED_SOLID;
        config->r = 100;
        config->g = 100;
        config->b = 100;
        config->w = 100;
        config->brightness = 100;
        config->speed = 10;
        config->on = true;
    }

    nvs_close(nvs_handle);
}

void led_save_to_nvs(led_persistent_config_t* config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(LED_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("LED", "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", config, sizeof(led_persistent_config_t));
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

// Current limiting helper function
void led_apply_current_limiting() {
    if (current_limit_ma <= 0 || led_buffer == nullptr || led_scaled_buffer == nullptr) {
        // No current limiting or buffers not ready - copy buffer as-is
        size_t bufSize = is_rgbw ? led_count * 4 : led_count * 3;
        memcpy(led_scaled_buffer, led_buffer, bufSize);
        return;
    }

    // Calculate total current consumption at full brightness
    uint32_t total_current_ma = 0;

    for (int i = 0; i < led_count; i++) {
        if (is_rgbw) {
            uint8_t g = led_buffer[i * 4 + 0];
            uint8_t r = led_buffer[i * 4 + 1];
            uint8_t b = led_buffer[i * 4 + 2];
            uint8_t w = led_buffer[i * 4 + 3];

            // Each channel at 255 uses 20mA, scale proportionally
            total_current_ma += (r * 20) / 255;
            total_current_ma += (g * 20) / 255;
            total_current_ma += (b * 20) / 255;
            total_current_ma += (w * 20) / 255;
        }
        else {
            uint8_t g = led_buffer[i * 3 + 0];
            uint8_t r = led_buffer[i * 3 + 1];
            uint8_t b = led_buffer[i * 3 + 2];

            total_current_ma += (r * 20) / 255;
            total_current_ma += (g * 20) / 255;
            total_current_ma += (b * 20) / 255;
        }
    }

    // Reserve 400mA for other system components
    uint32_t available_current_ma = (current_limit_ma > 400) ? (current_limit_ma - 400) : 0;

    if (total_current_ma <= available_current_ma) {
        // Within limits - copy buffer as-is
        size_t bufSize = is_rgbw ? led_count * 4 : led_count * 3;
        memcpy(led_scaled_buffer, led_buffer, bufSize);
        return;
    }

    // Scale down to fit within current limit
    uint32_t scale_factor_256 = (available_current_ma * 256) / total_current_ma;

    for (int i = 0; i < led_count; i++) {
        if (is_rgbw) {
            led_scaled_buffer[i * 4 + 0] = (led_buffer[i * 4 + 0] * scale_factor_256) / 256;  // G
            led_scaled_buffer[i * 4 + 1] = (led_buffer[i * 4 + 1] * scale_factor_256) / 256;  // R
            led_scaled_buffer[i * 4 + 2] = (led_buffer[i * 4 + 2] * scale_factor_256) / 256;  // B
            led_scaled_buffer[i * 4 + 3] = (led_buffer[i * 4 + 3] * scale_factor_256) / 256;  // W
        }
        else {
            led_scaled_buffer[i * 3 + 0] = (led_buffer[i * 3 + 0] * scale_factor_256) / 256;  // G
            led_scaled_buffer[i * 3 + 1] = (led_buffer[i * 3 + 1] * scale_factor_256) / 256;  // R
            led_scaled_buffer[i * 3 + 2] = (led_buffer[i * 3 + 2] * scale_factor_256) / 256;  // B
        }
    }
}