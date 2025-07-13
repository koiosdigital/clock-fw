#include "fibonacci_handlers.h"
#include "fibonacci/clock.h"
#include "fibonacci/themes.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define FIBONACCI_NVS_NAMESPACE "fib_cfg"

static const char* TAG = "FIBONACCI_API_HANDLERS";

esp_err_t fibonacci_config_get_handler(httpd_req_t* req) {
    fibonacci_config_t config;
    fibonacci_load_from_nvs(&config);

    const fibonacci_colorTheme* theme_info = fibonacci_get_theme_info(config.theme_id);

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* brightness_json = cJSON_CreateNumber(config.brightness);
    cJSON* theme_id_json = cJSON_CreateNumber(config.theme_id);
    cJSON* theme_name_json = cJSON_CreateString(theme_info ? theme_info->name : "Unknown");
    cJSON* on_json = cJSON_CreateBool(config.on);

    cJSON_AddItemToObject(json, "brightness", brightness_json);
    cJSON_AddItemToObject(json, "theme_id", theme_id_json);
    cJSON_AddItemToObject(json, "theme_name", theme_name_json);
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
        ESP_LOGE(TAG, "Invalid JSON received");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Validate and extract fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* theme_id_json = cJSON_GetObjectItem(json, "theme_id");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    fibonacci_config_t config;
    fibonacci_load_from_nvs(&config);

    // Validate brightness if present
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        config.brightness = (val < 0) ? 0 : (val > 255) ? 255 : val;
    }

    // Validate theme_id if present
    if (cJSON_IsNumber(theme_id_json)) {
        int val = cJSON_GetNumberValue(theme_id_json);
        if (val >= 0 && val < fibonacci_get_themes_count()) {
            config.theme_id = val;
        }
    }

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        config.on = cJSON_IsTrue(on_json);
    }

    cJSON_Delete(json);

    // Apply configuration changes
    fibonacci_apply_config(&config);
    fibonacci_save_to_nvs(&config);

    // Return success response
    const char* response = "{\"status\":\"success\"}";
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

void register_fibonacci_handlers(httpd_handle_t server) {
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
}

void fibonacci_load_from_nvs(fibonacci_config_t* config) {
    if (config == NULL) return;

    // Set defaults
    config->brightness = 128;
    config->theme_id = 0;
    config->on = true;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(FIBONACCI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace not found, using defaults");
        return;
    }

    size_t required_size = sizeof(fibonacci_config_t);
    err = nvs_get_blob(nvs_handle, "config", config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Config not found in NVS, using defaults");
    }
    else {
        ESP_LOGI(TAG, "Loaded config from NVS: brightness=%d, theme_id=%d, on=%d",
            config->brightness, config->theme_id, config->on);
    }

    nvs_close(nvs_handle);
}

void fibonacci_save_to_nvs(fibonacci_config_t* config) {
    if (config == NULL) return;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(FIBONACCI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", config, sizeof(fibonacci_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(err));
    }
    else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        }
        else {
            ESP_LOGI(TAG, "Config saved to NVS");
        }
    }

    nvs_close(nvs_handle);
}
