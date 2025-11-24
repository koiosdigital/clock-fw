#include "nixie_handlers.h"
#include "nixie.h"
#include "nixie_oe.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "api.h"  // For set_cors_headers function
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "nixie_handlers";

#define NIXIE_NVS_NAMESPACE "nixie_cfg"

// External nixie_config from nixie.cpp
extern nixie_config_t nixie_config;

// Helper function to create nixie state JSON
static cJSON* create_nixie_state_json(void) {
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON* brightness_json = cJSON_CreateNumber(nixie_config.brightness);
    cJSON* military_time_json = cJSON_CreateBool(nixie_config.military_time);
    cJSON* blinking_dots_json = cJSON_CreateBool(nixie_config.blinking_dots);
    cJSON* on_json = cJSON_CreateBool(nixie_config.on);

    cJSON_AddItemToObject(json, "brightness", brightness_json);
    cJSON_AddItemToObject(json, "military_time", military_time_json);
    cJSON_AddItemToObject(json, "blinking_dots", blinking_dots_json);
    cJSON_AddItemToObject(json, "on", on_json);

    return json;
}

// Helper function to apply nixie configuration from JSON
static esp_err_t apply_nixie_config_from_json(cJSON* json) {
    if (!json) return ESP_FAIL;

    // Get fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* military_time_json = cJSON_GetObjectItem(json, "military_time");
    cJSON* blinking_dots_json = cJSON_GetObjectItem(json, "blinking_dots");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    nixie_config_t new_config = nixie_config;  // Start with current config

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

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        new_config.on = cJSON_IsTrue(on_json);
    }

    // Apply the configuration
    nixie_set_config(&new_config);
    return ESP_OK;
}

// NVS functions
void nixie_load_from_nvs(nixie_config_t* config) {
    if (!config) return;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NIXIE_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace not found, using defaults");
        return;
    }

    size_t required_size = sizeof(nixie_config_t);
    err = nvs_get_blob(nvs_handle, "config", config, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Config not found in NVS, using defaults");
    }
    else {
        ESP_LOGI(TAG, "Loaded nixie config from NVS: brightness=%d, military=%d, dots=%d, on=%d",
            config->brightness, config->military_time, config->blinking_dots, config->on);
    }

    nvs_close(nvs_handle);
}

void nixie_save_to_nvs(const nixie_config_t* config) {
    if (!config) return;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NIXIE_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", config, sizeof(nixie_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(err));
    }
    else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        }
        else {
            ESP_LOGI(TAG, "Nixie config saved to NVS");
        }
    }

    nvs_close(nvs_handle);
}

// Configuration getters/setters
nixie_config_t nixie_get_config(void) {
    return nixie_config;
}

void nixie_set_config(const nixie_config_t* config) {
    if (!config) return;

    nixie_config = *config;
    nixie_apply_config(config);
    nixie_save_to_nvs(config);
}

void nixie_apply_config(const nixie_config_t* config) {
    if (!config) return;

    // Apply brightness setting
    nixie_set_brightness(config->brightness);

    ESP_LOGI(TAG, "Applied nixie config: brightness=%d, military=%d, dots=%d, on=%d",
        config->brightness, config->military_time, config->blinking_dots, config->on);
}

// Legacy HTTP handlers for backward compatibility
esp_err_t nixie_config_get_handler(httpd_req_t* req) {
    cJSON* json = create_nixie_state_json();
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Apply configuration using shared function
    esp_err_t result = apply_nixie_config_from_json(json);
    cJSON_Delete(json);

    if (result != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return ESP_FAIL;
    }

    return nixie_config_get_handler(req); // Return updated config
}

void register_nixie_handlers(httpd_handle_t server) {
    // Register legacy HTTP handlers for backward compatibility
    httpd_uri_t nixie_config_get_uri = {
        .uri = "/api/nixie",
        .method = HTTP_GET,
        .handler = nixie_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &nixie_config_get_uri);

    httpd_uri_t nixie_config_post_uri = {
        .uri = "/api/nixie",
        .method = HTTP_POST,
        .handler = nixie_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &nixie_config_post_uri);
}
