#include "api.h"

#include "mdns.h"
#include "esp_http_server.h"
#include "kd_common.h"
#include "cJSON.h"
#include "internet_time.h"
#include "embedded_tz_db.h"
#include <esp_app_desc.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Empty handle to esp_http_server */
httpd_handle_t kd_server = NULL;

httpd_handle_t get_httpd_handle() {
    return kd_server;
}

//subtype
#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE
const char* subtype = "nixie";
#elif CONFIG_BASE_CLOCK_TYPE_FIBONACCI
const char* subtype = "fibonacci";
#elif CONFIG_BASE_CLOCK_TYPE_WORDCLOCK
const char* subtype = "wordclock";
#else
#error "No base clock type selected"
#endif

void init_mdns() {
    mdns_init();
    const char* hostname = kd_common_get_wifi_hostname();
    mdns_hostname_set(hostname);

    //esp_app_desc
    const esp_app_desc_t* app_desc = esp_app_get_description();

    mdns_txt_item_t serviceTxtData[4] = {
        {"model", FIRMWARE_VARIANT},
        {"type", "clock"},
        {"subtype", subtype},
        { "version", app_desc->version }
    };

    ESP_ERROR_CHECK(mdns_service_add(NULL, "_koiosdigital", "_tcp", 80, serviceTxtData, 4));
}

void server_init() {
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 50;

    httpd_start(&kd_server, &config);
}

esp_err_t root_handler(httpd_req_t* req) {
    const char* response = "Welcome to the KD Clock API!";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t about_handler(httpd_req_t* req) {
    const esp_app_desc_t* app_desc = esp_app_get_description();

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* model = cJSON_CreateString(FIRMWARE_VARIANT);
    cJSON* type = cJSON_CreateString("clock");
    cJSON* subtype_json = cJSON_CreateString(subtype);
    cJSON* version = cJSON_CreateString(app_desc->version);

    cJSON_AddItemToObject(json, "model", model);
    cJSON_AddItemToObject(json, "type", type);
    cJSON_AddItemToObject(json, "subtype", subtype_json);
    cJSON_AddItemToObject(json, "version", version);

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

esp_err_t system_config_get_handler(httpd_req_t* req) {
    time_config_t config = time_get_config();
    char* wifi_hostname = kd_common_get_wifi_hostname();

    // Create JSON response
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        if (wifi_hostname) free(wifi_hostname);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON* auto_timezone = cJSON_CreateBool(config.auto_timezone);
    cJSON* timezone = cJSON_CreateString(config.timezone);
    cJSON* ntp_server = cJSON_CreateString(config.ntp_server);
    cJSON* wifi_hostname_json = cJSON_CreateString(wifi_hostname ? wifi_hostname : "");

    cJSON_AddItemToObject(json, "auto_timezone", auto_timezone);
    cJSON_AddItemToObject(json, "timezone", timezone);
    cJSON_AddItemToObject(json, "ntp_server", ntp_server);
    cJSON_AddItemToObject(json, "wifi_hostname", wifi_hostname_json);

    char* json_string = cJSON_Print(json);
    if (json_string == NULL) {
        cJSON_Delete(json);
        if (wifi_hostname) free(wifi_hostname);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));

    free(json_string);
    cJSON_Delete(json);

    return ESP_OK;
}

esp_err_t system_config_post_handler(httpd_req_t* req) {
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

    // Get current config as starting point
    time_config_t new_config = time_get_config();

    // Validate and extract fields
    cJSON* auto_timezone_json = cJSON_GetObjectItem(json, "auto_timezone");
    cJSON* timezone_json = cJSON_GetObjectItem(json, "timezone");
    cJSON* ntp_server_json = cJSON_GetObjectItem(json, "ntp_server");
    cJSON* wifi_hostname_json = cJSON_GetObjectItem(json, "wifi_hostname");

    // Validate auto_timezone if present
    if (cJSON_IsBool(auto_timezone_json)) {
        new_config.auto_timezone = cJSON_IsTrue(auto_timezone_json);
    }

    // Validate timezone if present
    if (cJSON_IsString(timezone_json)) {
        const char* tz_str = cJSON_GetStringValue(timezone_json);
        if (strlen(tz_str) < sizeof(new_config.timezone)) {
            strncpy(new_config.timezone, tz_str, sizeof(new_config.timezone) - 1);
            new_config.timezone[sizeof(new_config.timezone) - 1] = '\0';
        }
    }

    // Validate ntp_server if present
    if (cJSON_IsString(ntp_server_json)) {
        const char* ntp_str = cJSON_GetStringValue(ntp_server_json);
        if (strlen(ntp_str) < sizeof(new_config.ntp_server)) {
            strncpy(new_config.ntp_server, ntp_str, sizeof(new_config.ntp_server) - 1);
            new_config.ntp_server[sizeof(new_config.ntp_server) - 1] = '\0';
        }
    }

    // Validate wifi_hostname if present
    bool wifi_hostname_updated = false;
    if (cJSON_IsString(wifi_hostname_json)) {
        const char* hostname_str = cJSON_GetStringValue(wifi_hostname_json);
        if (hostname_str && strlen(hostname_str) > 0 && strlen(hostname_str) <= 63) {
            kd_common_set_wifi_hostname(hostname_str);
            wifi_hostname_updated = true;
        }
    }

    cJSON_Delete(json);

    // Apply configuration
    time_set_config(&new_config);

    // Create dynamic response message
    cJSON* response_json = cJSON_CreateObject();
    cJSON* status = cJSON_CreateString("success");
    cJSON* message = cJSON_CreateString(wifi_hostname_updated ?
        "System configuration updated. WiFi hostname change requires restart to take effect." :
        "System configuration updated. Restart required to apply changes.");

    cJSON_AddItemToObject(response_json, "status", status);
    cJSON_AddItemToObject(response_json, "message", message);

    char* response_string = cJSON_Print(response_json);
    if (response_string == NULL) {
        cJSON_Delete(response_json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));

    free(response_string);
    cJSON_Delete(response_json);

    return ESP_OK;
}

esp_err_t time_zones_handler(httpd_req_t* req) {
    const embeddedTz_t* zones = tz_db_get_all_zones();

    // Set content type and start chunked response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");

    // Send opening bracket for JSON array
    httpd_resp_send_chunk(req, "[", 1);

    // Process zones in chunks to avoid memory issues
    const int CHUNK_SIZE = 20;  // Process 20 zones at a time
    bool first_zone = true;

    for (int chunk_start = 0; chunk_start < TZ_DB_NUM_ZONES; chunk_start += CHUNK_SIZE) {
        int chunk_end = chunk_start + CHUNK_SIZE;
        if (chunk_end > TZ_DB_NUM_ZONES) {
            chunk_end = TZ_DB_NUM_ZONES;
        }

        // Create JSON array for this chunk
        cJSON* chunk_array = cJSON_CreateArray();
        if (chunk_array == NULL) {
            httpd_resp_send_chunk(req, NULL, 0);  // End chunked response
            return ESP_FAIL;
        }

        // Add zones to this chunk
        for (int i = chunk_start; i < chunk_end; i++) {
            cJSON* zone_obj = cJSON_CreateObject();
            if (zone_obj == NULL) continue;

            cJSON* name = cJSON_CreateString(zones[i].name);
            cJSON* rule = cJSON_CreateString(zones[i].rule);

            cJSON_AddItemToObject(zone_obj, "name", name);
            cJSON_AddItemToObject(zone_obj, "rule", rule);
            cJSON_AddItemToArray(chunk_array, zone_obj);
        }

        // Convert chunk to string
        char* chunk_string = cJSON_PrintUnformatted(chunk_array);
        if (chunk_string == NULL) {
            cJSON_Delete(chunk_array);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }

        // Remove the outer brackets from the chunk JSON array
        // (we'll manually manage the main array brackets)
        size_t chunk_len = strlen(chunk_string);
        if (chunk_len > 2) {  // Remove '[' and ']'
            chunk_string[chunk_len - 1] = '\0';  // Remove ']'
            char* content = chunk_string + 1;     // Skip '['

            // Add comma separator if not first zone
            if (!first_zone) {
                httpd_resp_send_chunk(req, ",", 1);
            }

            // Send the chunk content
            httpd_resp_send_chunk(req, content, strlen(content));
            first_zone = false;
        }

        free(chunk_string);
        cJSON_Delete(chunk_array);

        // Small delay to prevent overwhelming the system
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Send closing bracket for JSON array
    httpd_resp_send_chunk(req, "]", 1);

    // End chunked response
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

void api_init() {
    init_mdns();
    server_init();

    httpd_handle_t server = get_httpd_handle();
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t about_uri = {
        .uri = "/api/system/about",
        .method = HTTP_GET,
        .handler = about_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &about_uri);

    httpd_uri_t system_config_get_uri = {
        .uri = "/api/system/config",
        .method = HTTP_GET,
        .handler = system_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &system_config_get_uri);

    httpd_uri_t system_config_post_uri = {
        .uri = "/api/system/config",
        .method = HTTP_POST,
        .handler = system_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &system_config_post_uri);

    httpd_uri_t time_zones_uri = {
        .uri = "/api/time/zonedb",
        .method = HTTP_GET,
        .handler = time_zones_handler,
        .user_ctx = NULL
    };    httpd_register_uri_handler(server, &time_zones_uri);

    register_led_handlers(server);
}