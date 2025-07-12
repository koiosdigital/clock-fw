#include "handlers.h"
#include "led.h"
#include "clock.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "API_HANDLERS";

esp_err_t led_config_get_handler(httpd_req_t* req) {
    // Get current LED configuration
    LEDEffect_t current_effect = led_get_effect();
    uint8_t r, g, b, w;
    led_get_color(&r, &g, &b, &w);
    uint8_t brightness = led_get_brightness();

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Map effect enum to string
    const char* effect_str = "unknown";
    switch (current_effect) {
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
    cJSON* red = cJSON_CreateNumber(r);
    cJSON* green = cJSON_CreateNumber(g);
    cJSON* blue = cJSON_CreateNumber(b);
    cJSON* white = cJSON_CreateNumber(w);
    cJSON* brightness_json = cJSON_CreateNumber(brightness);

    cJSON_AddItemToObject(color, "r", red);
    cJSON_AddItemToObject(color, "g", green);
    cJSON_AddItemToObject(color, "b", blue);
    cJSON_AddItemToObject(color, "w", white);

    cJSON_AddItemToObject(json, "mode", mode);
    cJSON_AddItemToObject(json, "color", color);
    cJSON_AddItemToObject(json, "brightness", brightness_json);

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
        ESP_LOGE(TAG, "Invalid JSON received");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Validate and extract fields
    cJSON* mode_json = cJSON_GetObjectItem(json, "mode");
    cJSON* color_json = cJSON_GetObjectItem(json, "color");
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");

    // Validate mode
    LEDEffect_t new_effect = LED_OFF;
    if (cJSON_IsString(mode_json)) {
        const char* mode_str = cJSON_GetStringValue(mode_json);
        if (strcmp(mode_str, "off") == 0) new_effect = LED_OFF;
        else if (strcmp(mode_str, "solid") == 0) new_effect = LED_SOLID;
        else if (strcmp(mode_str, "blink") == 0) new_effect = LED_BLINK;
        else if (strcmp(mode_str, "breathe") == 0) new_effect = LED_BREATHE;
        else if (strcmp(mode_str, "cyclic") == 0) new_effect = LED_CYCLIC;
        else if (strcmp(mode_str, "rainbow") == 0) new_effect = LED_RAINBOW;
        else {
            ESP_LOGE(TAG, "Invalid mode: %s", mode_str);
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode");
            return ESP_FAIL;
        }
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
    }

    // Validate brightness if present
    uint8_t brightness = 255;
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        brightness = (val < 0) ? 0 : (val > 255) ? 255 : val;
    }

    cJSON_Delete(json);

    // Apply configuration
    led_set_effect(new_effect);
    led_set_color(r, g, b, w);
    led_set_brightness(brightness);

    ESP_LOGI(TAG, "LED config updated: mode=%d, color=(%d,%d,%d,%d), brightness=%d",
        new_effect, r, g, b, w, brightness);

    led_persistent_config_t config;
    config.effect = new_effect;
    config.r = r;
    config.g = g;
    config.b = b;
    config.w = w;
    config.brightness = brightness;
    config.speed = led_get_speed(); // Keep current speed

    led_set_persistent_config(&config);

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t nixie_config_get_handler(httpd_req_t* req) {
    // Get current Nixie configuration
    nixie_config_t config = nixie_get_config();

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* brightness = cJSON_CreateNumber(config.brightness);
    cJSON* military_time = cJSON_CreateBool(config.military_time);
    cJSON* blinking_dots = cJSON_CreateBool(config.blinking_dots);
    cJSON* enabled = cJSON_CreateBool(config.enabled);

    cJSON_AddItemToObject(json, "brightness", brightness);
    cJSON_AddItemToObject(json, "military_time", military_time);
    cJSON_AddItemToObject(json, "blinking_dots", blinking_dots);
    cJSON_AddItemToObject(json, "enabled", enabled);

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

esp_err_t nixie_config_post_handler(httpd_req_t* req) {
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
        ESP_LOGE(TAG, "Invalid JSON received");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Get current config as starting point
    nixie_config_t new_config = nixie_get_config();

    // Validate and extract fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* military_time_json = cJSON_GetObjectItem(json, "military_time");
    cJSON* blinking_dots_json = cJSON_GetObjectItem(json, "blinking_dots");
    cJSON* enabled_json = cJSON_GetObjectItem(json, "enabled");

    // Validate brightness if present
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        new_config.brightness = (val < 0) ? 0 : (val > 100) ? 100 : val;
    }

    // Validate military_time if present
    if (cJSON_IsBool(military_time_json)) {
        new_config.military_time = cJSON_IsTrue(military_time_json);
    }

    // Validate blinking_dots if present
    if (cJSON_IsBool(blinking_dots_json)) {
        new_config.blinking_dots = cJSON_IsTrue(blinking_dots_json);
    }

    // Validate enabled if present
    if (cJSON_IsBool(enabled_json)) {
        new_config.enabled = cJSON_IsTrue(enabled_json);
    }

    cJSON_Delete(json);

    // Apply configuration
    nixie_set_config(&new_config);

    ESP_LOGI(TAG, "Nixie config updated: brightness=%d, military_time=%s, blinking_dots=%s, enabled=%s",
        new_config.brightness,
        new_config.military_time ? "true" : "false",
        new_config.blinking_dots ? "true" : "false",
        new_config.enabled ? "true" : "false");

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

void register_api_handlers(httpd_handle_t server) {
    httpd_uri_t led_config_get_uri = {
        .uri = "/api/led/config",
        .method = HTTP_GET,
        .handler = led_config_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t led_config_post_uri = {
        .uri = "/api/led/config",
        .method = HTTP_POST,
        .handler = led_config_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t nixie_config_get_uri = {
        .uri = "/api/nixie/config",
        .method = HTTP_GET,
        .handler = nixie_config_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t nixie_config_post_uri = {
        .uri = "/api/nixie/config",
        .method = HTTP_POST,
        .handler = nixie_config_post_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &led_config_get_uri);
    httpd_register_uri_handler(server, &led_config_post_uri);
    httpd_register_uri_handler(server, &nixie_config_get_uri);
    httpd_register_uri_handler(server, &nixie_config_post_uri);
}
