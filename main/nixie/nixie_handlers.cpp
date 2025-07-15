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

// Structure to track WebSocket connections
typedef struct {
    httpd_handle_t server;
    int fd;
    bool active;
} nixie_ws_client_t;

#define MAX_WS_CLIENTS 5
static nixie_ws_client_t ws_clients[MAX_WS_CLIENTS];
static SemaphoreHandle_t ws_clients_mutex = NULL;

// External nixie_config from nixie.cpp
extern nixie_config_t nixie_config;

// Initialize WebSocket client tracking
static void init_ws_client_tracking(void) {
    if (ws_clients_mutex == NULL) {
        ws_clients_mutex = xSemaphoreCreateMutex();

        // Initialize all client slots as inactive
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            ws_clients[i].server = NULL;
            ws_clients[i].fd = -1;
            ws_clients[i].active = false;
        }

        ESP_LOGI(TAG, "Nixie WebSocket client tracking initialized");
    }
}

// Add a WebSocket client
static void add_ws_client(httpd_handle_t server, int fd) {
    init_ws_client_tracking();

    if (xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // First, check if this fd already exists (shouldn't happen, but be safe)
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active && ws_clients[i].fd == fd) {
                ESP_LOGW(TAG, "Nixie WebSocket client fd=%d already exists, updating", fd);
                ws_clients[i].server = server;
                xSemaphoreGive(ws_clients_mutex);
                return;
            }
        }

        // Find an empty slot
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (!ws_clients[i].active) {
                ws_clients[i].server = server;
                ws_clients[i].fd = fd;
                ws_clients[i].active = true;
                ESP_LOGI(TAG, "Added nixie WebSocket client: fd=%d, slot=%d", fd, i);
                break;
            }
        }
        xSemaphoreGive(ws_clients_mutex);
    }
}

// Remove a WebSocket client
static void remove_ws_client(int fd) {
    if (ws_clients_mutex && xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active && ws_clients[i].fd == fd) {
                ws_clients[i].active = false;
                ws_clients[i].fd = -1;  // Clear the fd
                ws_clients[i].server = NULL;  // Clear the server handle
                ESP_LOGI(TAG, "Removed nixie WebSocket client: fd=%d, slot=%d", fd, i);
                break;
            }
        }
        xSemaphoreGive(ws_clients_mutex);
    }
}

// Clean up stale WebSocket connections
static void cleanup_stale_clients(void) {
    if (ws_clients_mutex && xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int active_count = 0;
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active) {
                // Try to send a ping frame to check if connection is still alive
                httpd_ws_frame_t ping_frame = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_PING,
                    .payload = NULL,
                    .len = 0
                };

                esp_err_t ret = httpd_ws_send_frame_async(ws_clients[i].server, ws_clients[i].fd, &ping_frame);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Ping failed for nixie client fd=%d, slot=%d, cleaning up", ws_clients[i].fd, i);
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                }
                else {
                    active_count++;
                }
            }
        }
        ESP_LOGD(TAG, "Active nixie WebSocket clients: %d", active_count);
        xSemaphoreGive(ws_clients_mutex);
    }
}

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

    // Broadcast state to WebSocket clients
    nixie_broadcast_state();

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t nixie_websocket_handler(httpd_req_t* req) {
    int client_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Nixie WebSocket connection opened: fd=%d", client_fd);
        add_ws_client(req->handle, client_fd);

        // Send current nixie state immediately upon connection
        cJSON* state_json = create_nixie_state_json();
        if (state_json) {
            char* state_str = cJSON_Print(state_json);
            if (state_str) {
                httpd_ws_frame_t ws_pkt = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t*)state_str,
                    .len = strlen(state_str)
                };
                esp_err_t send_ret = httpd_ws_send_frame(req, &ws_pkt);
                if (send_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send initial nixie state to new client fd=%d: %s",
                        client_fd, esp_err_to_name(send_ret));
                    remove_ws_client(client_fd);
                }
                free(state_str);
            }
            cJSON_Delete(state_json);
        }

        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t* buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Set max_len = 0 to get the frame len
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        // Handle different error types appropriately
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Nixie WebSocket frame from fd=%d is not properly masked - ignoring frame", client_fd);
            // Don't remove client for masking errors, just ignore this frame
            return ESP_OK;
        }
        else {
            ESP_LOGW(TAG, "Failed to receive nixie WebSocket frame from fd=%d: %s", client_fd, esp_err_to_name(ret));
            remove_ws_client(client_fd);
            return ret;
        }
    }

    if (ws_pkt.len) {
        // Allocate buffer for frame content
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer for nixie WebSocket frame");
            remove_ws_client(client_fd);
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;

        // Receive the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "Nixie WebSocket payload from fd=%d is not properly masked - ignoring frame", client_fd);
                free(buf);
                // Don't remove client for masking errors, just ignore this frame
                return ESP_OK;
            }
            else {
                ESP_LOGW(TAG, "Failed to receive nixie WebSocket payload from fd=%d: %s", client_fd, esp_err_to_name(ret));
                free(buf);
                remove_ws_client(client_fd);
                return ret;
            }
        }
    }

    // Handle the frame based on its type
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGD(TAG, "Received nixie WebSocket text from fd=%d: %.*s", client_fd, (int)ws_pkt.len, (char*)ws_pkt.payload);

        cJSON* json = cJSON_Parse((char*)ws_pkt.payload);
        if (json) {
            if (apply_nixie_config_from_json(json) == ESP_OK) {
                // Clean up stale connections before broadcasting
                cleanup_stale_clients();
                // Broadcast updated state to all clients
                nixie_broadcast_state();
            }
            cJSON_Delete(json);
        }
        else {
            ESP_LOGW(TAG, "Invalid JSON received from nixie WebSocket client fd=%d", client_fd);
        }
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "Nixie WebSocket close frame received from fd=%d", client_fd);
        remove_ws_client(client_fd);
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        ESP_LOGD(TAG, "Nixie WebSocket pong received from fd=%d", client_fd);
        // Client is alive, no action needed
    }

    if (buf) {
        free(buf);
    }
    return ESP_OK;
}

void nixie_broadcast_state(void) {
    init_ws_client_tracking();

    cJSON* state_json = create_nixie_state_json();
    if (!state_json) {
        ESP_LOGE(TAG, "Failed to create nixie state JSON for broadcast");
        return;
    }

    char* state_str = cJSON_Print(state_json);
    if (!state_str) {
        cJSON_Delete(state_json);
        ESP_LOGE(TAG, "Failed to serialize nixie state JSON for broadcast");
        return;
    }

    if (xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int sent_count = 0;
        int cleaned_count = 0;

        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active) {
                httpd_ws_frame_t ws_pkt = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t*)state_str,
                    .len = strlen(state_str)
                };

                esp_err_t ret = httpd_ws_send_frame_async(ws_clients[i].server, ws_clients[i].fd, &ws_pkt);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send to nixie WebSocket client fd=%d, slot=%d: %s",
                        ws_clients[i].fd, i, esp_err_to_name(ret));
                    // Clean up stale connection immediately
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                    cleaned_count++;
                }
                else {
                    sent_count++;
                    ESP_LOGD(TAG, "Sent nixie state to WebSocket client fd=%d, slot=%d", ws_clients[i].fd, i);
                }
            }
        }

        if (sent_count > 0 || cleaned_count > 0) {
            ESP_LOGD(TAG, "Nixie broadcast complete: sent to %d clients, cleaned %d stale connections", sent_count, cleaned_count);
        }

        xSemaphoreGive(ws_clients_mutex);
    }

    free(state_str);
    cJSON_Delete(state_json);
}

void register_nixie_handlers(httpd_handle_t server) {
    // Register WebSocket handler for real-time nixie control
    httpd_uri_t nixie_ws_uri = {
        .uri = "/api/nixie/ws",
        .method = HTTP_GET,
        .handler = nixie_websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &nixie_ws_uri);

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
