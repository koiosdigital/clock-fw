#include "nixie_handlers.h"
#include "clock.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define NIXIE_NVS_NAMESPACE "nixie_cfg"

esp_err_t nixie_config_get_handler(httpd_req_t* req) {
    nixie_config_t config;
    nixie_load_from_nvs(&config);

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* brightness_json = cJSON_CreateNumber(config.brightness);
    cJSON* military_time_json = cJSON_CreateBool(config.military_time);
    cJSON* blinking_dots_json = cJSON_CreateBool(config.blinking_dots);
    cJSON* on_json = cJSON_CreateBool(config.on);

    cJSON_AddItemToObject(json, "brightness", brightness_json);
    cJSON_AddItemToObject(json, "military_time", military_time_json);
    cJSON_AddItemToObject(json, "blinking_dots", blinking_dots_json);
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

    // Validate and extract fields
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* military_time_json = cJSON_GetObjectItem(json, "military_time");
    cJSON* blinking_dots_json = cJSON_GetObjectItem(json, "blinking_dots");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    nixie_config_t config;
    nixie_load_from_nvs(&config);

    // Validate brightness if present
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        config.brightness = (val < 0) ? 0 : (val > 100) ? 100 : val;
    }

    // Validate military_time if present
    if (cJSON_IsBool(military_time_json)) {
        config.military_time = cJSON_IsTrue(military_time_json);
    }

    // Validate blinking_dots if present
    if (cJSON_IsBool(blinking_dots_json)) {
        config.blinking_dots = cJSON_IsTrue(blinking_dots_json);
    }

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        config.on = cJSON_IsTrue(on_json);
    }

    cJSON_Delete(json);

    // Apply configuration changes
    nixie_save_to_nvs(&config);
    nixie_load_and_apply_config();

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

void register_nixie_handlers(httpd_handle_t server) {
    httpd_uri_t nixie_config_get_uri = {
        .uri = "/api/nixie",
        .method = HTTP_GET,
        .handler = nixie_config_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t nixie_config_post_uri = {
        .uri = "/api/nixie",
        .method = HTTP_POST,
        .handler = nixie_config_post_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &nixie_config_get_uri);
    httpd_register_uri_handler(server, &nixie_config_post_uri);
}

void nixie_load_from_nvs(nixie_config_t* config) {
    if (config == NULL) return;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NIXIE_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        config->brightness = 80;
        config->military_time = false;
        config->blinking_dots = true;
        config->on = true;
        return;
    }

    size_t required_size = sizeof(nixie_config_t);
    err = nvs_get_blob(nvs_handle, "config", config, &required_size);
    if (err != ESP_OK) {
        config->brightness = 80;
        config->military_time = false;
        config->blinking_dots = true;
        config->on = true;
    }

    nvs_close(nvs_handle);
}

void nixie_save_to_nvs(nixie_config_t* config) {
    if (config == NULL) return;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NIXIE_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_blob(nvs_handle, "config", config, sizeof(nixie_config_t));
    if (err != ESP_OK) {
    }
    else {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
}
