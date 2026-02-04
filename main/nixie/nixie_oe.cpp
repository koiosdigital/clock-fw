#include "nixie_oe.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

static const char* TAG = "nixie_oe";

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE

static gpio_num_t oe_pin = (gpio_num_t)CONFIG_NIXIE_BRIGHTNESS_PIN;

void nixie_oe_init(void) {
    ESP_LOGI(TAG, "Initializing OE PWM on pin %d", oe_pin);

    // Configure PWM via LEDC for output enable pin
    ledc_timer_config_t pwm_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&pwm_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return;
    }

    ledc_channel_config_t pwm_channel = {
        .gpio_num = oe_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0, // Will be set by nixie_set_brightness
        .hpoint = 0
    };
    ret = ledc_channel_config(&pwm_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "OE PWM initialized successfully");
}

void nixie_set_brightness(uint8_t brightness_percent) {
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    // Map 0-100% to 20-100% duty cycle, then invert (since OE is active low)
    float actual_duty = 20.0f + (brightness_percent * 80.0f / 100.0f);

#ifdef CONFIG_NIXIE_BRIGHTNESS_INVERTED
    actual_duty = 100.0f - actual_duty;
#endif

    uint32_t duty_value = (uint32_t)(actual_duty * ((1 << 8) - 1) / 100.0f);

    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty cycle: %s", esp_err_to_name(ret));
        return;
    }

    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty cycle: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGD(TAG, "Set brightness to %d%% (duty: %.1f%%, value: %lu)", brightness_percent, actual_duty, duty_value);
}

gpio_num_t nixie_oe_get_pin(void) {
    return oe_pin;
}

#endif