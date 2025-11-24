#include "fibonacci_handlers.h"
#include "fibonacci.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include "api.h"  // For set_cors_headers function
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "fibonacci_handlers";

// NVS namespace for Fibonacci configuration
#define FIBONACCI_NVS_NAMESPACE "fib_cfg"

// Helper function to create Fibonacci state JSON
static cJSON* create_fibonacci_state_json(void) {
    fibonacci_config_t config;
    fibonacci_load_from_nvs(&config);

    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON* brightness_json = cJSON_CreateNumber(config.brightness);
    cJSON* theme_id_json = cJSON_CreateNumber(config.theme_id);
    cJSON* on_json = cJSON_CreateBool(config.on);

    // Add theme information
    cJSON* themes_array = cJSON_CreateArray();
    uint8_t themes_count = fibonacci_get_themes_count();
    for (uint8_t i = 0; i < themes_count; i++) {
        const fibonacci_colorTheme* theme = fibonacci_get_theme_info(i);
        if (theme) {
            cJSON* theme_obj = cJSON_CreateObject();
            cJSON_AddItemToObject(theme_obj, "id", cJSON_CreateNumber(theme->id));
            cJSON_AddItemToObject(theme_obj, "name", cJSON_CreateString(theme->name));

            // Convert colors to hex strings
            char hour_color_str[8], minute_color_str[8], both_color_str[8];
            snprintf(hour_color_str, sizeof(hour_color_str), "#%06X", (unsigned int)theme->hour_color);
            snprintf(minute_color_str, sizeof(minute_color_str), "#%06X", (unsigned int)theme->minute_color);
            snprintf(both_color_str, sizeof(both_color_str), "#%06X", (unsigned int)theme->both_color);

            cJSON_AddItemToObject(theme_obj, "hour_color", cJSON_CreateString(hour_color_str));
            cJSON_AddItemToObject(theme_obj, "minute_color", cJSON_CreateString(minute_color_str));
            cJSON_AddItemToObject(theme_obj, "both_color", cJSON_CreateString(both_color_str));

            cJSON_AddItemToArray(themes_array, theme_obj);
        }
    }

    cJSON_AddItemToObject(json, "brightness", brightness_json);
    cJSON_AddItemToObject(json, "theme_id", theme_id_json);
    cJSON_AddItemToObject(json, "on", on_json);
    cJSON_AddItemToObject(json, "themes", themes_array);

    return json;
}

// Helper function to apply Fibonacci configuration from JSON
static esp_err_t apply_fibonacci_config_from_json(cJSON* json) {
    if (!json) return ESP_FAIL;

    // Get current config
    fibonacci_config_t config;
    fibonacci_load_from_nvs(&config);

    // Get fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* theme_id_json = cJSON_GetObjectItem(json, "theme_id");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    // Validate brightness if present
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        uint8_t brightness = (val < 0) ? 0 : (val > 255) ? 255 : val;
        config.brightness = brightness;
        fibonacci_set_brightness(brightness);
    }

    // Validate theme_id if present
    if (cJSON_IsNumber(theme_id_json)) {
        int val = cJSON_GetNumberValue(theme_id_json);
        uint8_t theme_id = (val < 0) ? 0 : (val >= fibonacci_get_themes_count()) ? 0 : val;
        config.theme_id = theme_id;
        fibonacci_set_theme(theme_id);
    }

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        bool on = cJSON_IsTrue(on_json);
        config.on = on;
        fibonacci_set_on_state(on);
    }

    // Save to NVS
    fibonacci_save_to_nvs(&config);
    return ESP_OK;
}

// Legacy HTTP handlers for backward compatibility
esp_err_t fibonacci_config_get_handler(httpd_req_t* req) {
    cJSON* json = create_fibonacci_state_json();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

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

esp_err_t fibonacci_config_post_handler(httpd_req_t* req) {
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

    // Apply configuration using shared function
    esp_err_t result = apply_fibonacci_config_from_json(json);
    cJSON_Delete(json);

    if (result != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return ESP_FAIL;
    }

    return fibonacci_config_get_handler(req); // Return updated config
}

// NVS functions
void fibonacci_load_from_nvs(fibonacci_config_t* config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(FIBONACCI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Fibonacci NVS namespace not found, using defaults");
        config->brightness = 255;
        config->theme_id = 0;
        config->on = true;
        return;
    }

    size_t required_size = sizeof(fibonacci_config_t);
    err = nvs_get_blob(nvs_handle, "config", config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Fibonacci config not found in NVS, using defaults");
        config->brightness = 255;
        config->theme_id = 0;
        config->on = true;
    }

    nvs_close(nvs_handle);
}

void fibonacci_save_to_nvs(fibonacci_config_t* config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(FIBONACCI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open Fibonacci NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", config, sizeof(fibonacci_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Fibonacci config to NVS: %s", esp_err_to_name(err));
    }
    else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit Fibonacci NVS: %s", esp_err_to_name(err));
        }
    }

    nvs_close(nvs_handle);
}

void register_fibonacci_handlers(httpd_handle_t server) {
    // Register legacy HTTP handlers for backward compatibility
    httpd_uri_t fibonacci_config_get_uri = {
        .uri = "/api/fibonacci",
        .method = HTTP_GET,
        .handler = fibonacci_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &fibonacci_config_get_uri);

    httpd_uri_t fibonacci_config_post_uri = {
        .uri = "/api/fibonacci",
        .method = HTTP_POST,
        .handler = fibonacci_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &fibonacci_config_post_uri);
}
