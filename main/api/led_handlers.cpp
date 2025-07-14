#include "led_handlers.h"
#include "led.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include "api.h"  // For set_cors_headers function

esp_err_t led_config_get_handler(httpd_req_t* req) {
    // Set CORS headers
    set_cors_headers(req);

    led_persistent_config_t config;
    led_load_from_nvs(&config);

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Map effect enum to string
    const char* effect_str = "unknown";
    switch (config.effect) {
    case LED_OFF: effect_str = "off"; break;
    case LED_SOLID: effect_str = "solid"; break;
    case LED_BLINK: effect_str = "blink"; break;
    case LED_BREATHE: effect_str = "breathe"; break;
    case LED_CYCLIC: effect_str = "cyclic"; break;
    case LED_RAINBOW: effect_str = "rainbow"; break;
    default: break;
    }

    cJSON* mode = cJSON_CreateString(effect_str);
    cJSON* color = cJSON_CreateObject();
    cJSON* red = cJSON_CreateNumber(config.r);
    cJSON* green = cJSON_CreateNumber(config.g);
    cJSON* blue = cJSON_CreateNumber(config.b);
    cJSON* brightness_json = cJSON_CreateNumber(config.brightness);
    cJSON* speed_json = cJSON_CreateNumber(config.speed);
    cJSON* on_json = cJSON_CreateBool(config.on);

    cJSON_AddItemToObject(color, "r", red);
    cJSON_AddItemToObject(color, "g", green);
    cJSON_AddItemToObject(color, "b", blue);

    if (led_is_rgbw()) {
        cJSON* white = cJSON_CreateNumber(config.w);
        cJSON_AddItemToObject(color, "w", white);
    }

    cJSON_AddItemToObject(json, "mode", mode);
    cJSON_AddItemToObject(json, "color", color);
    cJSON_AddItemToObject(json, "brightness", brightness_json);
    cJSON_AddItemToObject(json, "speed", speed_json);
    cJSON_AddItemToObject(json, "on", on_json);

    char* json_string = cJSON_Print(json);
    if (json_string == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));

    free(json_string);
    cJSON_Delete(json);

    return ESP_OK;
}

esp_err_t led_config_post_handler(httpd_req_t* req) {
    // Set CORS headers
    set_cors_headers(req);

    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        else {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse JSON
    cJSON* json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Validate and extract fields
    cJSON* mode_json = cJSON_GetObjectItem(json, "mode");
    cJSON* color_json = cJSON_GetObjectItem(json, "color");
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* speed_json = cJSON_GetObjectItem(json, "speed");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    led_persistent_config_t config;
    led_load_from_nvs(&config);

    // Validate mode
    LEDEffect_t new_effect = LED_OFF;
    if (cJSON_IsString(mode_json)) {
        const char* mode_str = cJSON_GetStringValue(mode_json);
        if (strcmp(mode_str, "solid") == 0) new_effect = LED_SOLID;
        else if (strcmp(mode_str, "blink") == 0) new_effect = LED_BLINK;
        else if (strcmp(mode_str, "breathe") == 0) new_effect = LED_BREATHE;
        else if (strcmp(mode_str, "cyclic") == 0) new_effect = LED_CYCLIC;
        else if (strcmp(mode_str, "rainbow") == 0) new_effect = LED_RAINBOW;
        else {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode");
            return ESP_FAIL;
        }
        led_set_effect(new_effect);
        config.effect = new_effect;
    }

    // Validate color if present
    uint8_t r = 0, g = 0, b = 0, w = 0;
    if (cJSON_IsObject(color_json)) {
        cJSON* red_json = cJSON_GetObjectItem(color_json, "r");
        cJSON* green_json = cJSON_GetObjectItem(color_json, "g");
        cJSON* blue_json = cJSON_GetObjectItem(color_json, "b");
        cJSON* white_json = cJSON_GetObjectItem(color_json, "w");

        if (cJSON_IsNumber(red_json)) {
            int val = cJSON_GetNumberValue(red_json);
            r = (val < 0) ? 0 : (val > 255) ? 255 : val;
        }
        if (cJSON_IsNumber(green_json)) {
            int val = cJSON_GetNumberValue(green_json);
            g = (val < 0) ? 0 : (val > 255) ? 255 : val;
        }
        if (cJSON_IsNumber(blue_json)) {
            int val = cJSON_GetNumberValue(blue_json);
            b = (val < 0) ? 0 : (val > 255) ? 255 : val;
        }
        if (cJSON_IsNumber(white_json)) {
            int val = cJSON_GetNumberValue(white_json);
            w = (val < 0) ? 0 : (val > 255) ? 255 : val;
        }
        led_set_color(r, g, b, w);
        config.r = r;
        config.g = g;
        config.b = b;
        config.w = w;
    }

    // Validate brightness if present
    uint8_t brightness = 255;
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        brightness = (val < 0) ? 0 : (val > 255) ? 255 : val;
        led_set_brightness(brightness);
        config.brightness = brightness;
    }

    // Validate speed if present
    uint8_t speed = 0; // Default speed
    if (cJSON_IsNumber(speed_json)) {
        int val = cJSON_GetNumberValue(speed_json);
        speed = (val < 1) ? 1 : (val > 100) ? 100 : val;
        led_set_speed(speed);
        config.speed = speed;
    }

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        bool on = cJSON_IsTrue(on_json);
        led_set_on_state(on);
        config.on = on;
    }

    cJSON_Delete(json);
    led_save_to_nvs(&config);

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

void register_led_handlers(httpd_handle_t server) {
    httpd_uri_t led_config_get_uri = {
        .uri = "/api/leds",
        .method = HTTP_GET,
        .handler = led_config_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t led_config_post_uri = {
        .uri = "/api/leds",
        .method = HTTP_POST,
        .handler = led_config_post_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &led_config_get_uri);
    httpd_register_uri_handler(server, &led_config_post_uri);
}
