#include "handlers.h"
#include "clock.h"
#include "themes.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "fibonacci_handlers";

esp_err_t fibonacci_config_get_handler(httpd_req_t* req) {
    // Get current fibonacci configuration
    fibonacci_config_t config = fibonacci_get_config();
    const fibonacci_colorTheme* theme_info = fibonacci_get_theme_info(config.theme_id);

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* brightness = cJSON_CreateNumber(config.brightness);
    cJSON* theme_id = cJSON_CreateNumber(config.theme_id);
    cJSON* theme_name = cJSON_CreateString(theme_info ? theme_info->name : "Unknown");

    cJSON_AddItemToObject(json, "brightness", brightness);
    cJSON_AddItemToObject(json, "theme_id", theme_id);
    cJSON_AddItemToObject(json, "theme_name", theme_name);

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
    char content[256];
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

    // Get current config as starting point
    fibonacci_config_t new_config = fibonacci_get_config();

    // Validate and extract fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* theme_id_json = cJSON_GetObjectItem(json, "theme_id");

    // Validate brightness if present
    if (cJSON_IsNumber(brightness_json)) {
        int brightness_val = cJSON_GetNumberValue(brightness_json);
        if (brightness_val >= 0 && brightness_val <= 255) {
            new_config.brightness = (uint8_t)brightness_val;
        }
    }

    // Validate theme_id if present
    if (cJSON_IsNumber(theme_id_json)) {
        int theme_val = cJSON_GetNumberValue(theme_id_json);
        if (theme_val >= 0 && theme_val < fibonacci_get_themes_count()) {
            new_config.theme_id = (uint8_t)theme_val;
        }
    }

    cJSON_Delete(json);

    // Apply configuration
    fibonacci_set_config(&new_config);

    // Return success response
    const char* response = "{\"status\":\"success\",\"message\":\"Fibonacci configuration updated\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t fibonacci_themes_handler(httpd_req_t* req) {
    // Create JSON array response
    cJSON* json = cJSON_CreateArray();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint8_t theme_count = fibonacci_get_themes_count();
    for (uint8_t i = 0; i < theme_count; i++) {
        const fibonacci_colorTheme* theme = fibonacci_get_theme_info(i);
        if (theme == NULL) continue;

        cJSON* theme_obj = cJSON_CreateObject();
        if (theme_obj == NULL) continue;

        cJSON* id = cJSON_CreateNumber(theme->id);
        cJSON* name = cJSON_CreateString(theme->name);
        cJSON* hour_color = cJSON_CreateNumber(theme->hour_color);
        cJSON* minute_color = cJSON_CreateNumber(theme->minute_color);
        cJSON* both_color = cJSON_CreateNumber(theme->both_color);

        cJSON_AddItemToObject(theme_obj, "id", id);
        cJSON_AddItemToObject(theme_obj, "name", name);
        cJSON_AddItemToObject(theme_obj, "hour_color", hour_color);
        cJSON_AddItemToObject(theme_obj, "minute_color", minute_color);
        cJSON_AddItemToObject(theme_obj, "both_color", both_color);

        cJSON_AddItemToArray(json, theme_obj);
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

void register_api_handlers(httpd_handle_t server) {
    httpd_uri_t fibonacci_config_get_uri = {
        .uri = "/api/fibonacci/config",
        .method = HTTP_GET,
        .handler = fibonacci_config_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t fibonacci_config_post_uri = {
        .uri = "/api/fibonacci/config",
        .method = HTTP_POST,
        .handler = fibonacci_config_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t fibonacci_themes_uri = {
        .uri = "/api/fibonacci/themes",
        .method = HTTP_GET,
        .handler = fibonacci_themes_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &fibonacci_config_get_uri);
    httpd_register_uri_handler(server, &fibonacci_config_post_uri);
    httpd_register_uri_handler(server, &fibonacci_themes_uri);

    ESP_LOGI(TAG, "Fibonacci API handlers registered");
}
