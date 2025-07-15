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

// Structure to track WebSocket connections
typedef struct {
    httpd_handle_t server;
    int fd;
    bool active;
} fibonacci_ws_client_t;

#define MAX_WS_CLIENTS 5
static fibonacci_ws_client_t ws_clients[MAX_WS_CLIENTS];
static SemaphoreHandle_t ws_clients_mutex = NULL;

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

        ESP_LOGI(TAG, "Fibonacci WebSocket client tracking initialized");
    }
}

// Add a WebSocket client
static void add_ws_client(httpd_handle_t server, int fd) {
    init_ws_client_tracking();

    if (xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // First, check if this fd already exists (shouldn't happen, but be safe)
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active && ws_clients[i].fd == fd) {
                ESP_LOGW(TAG, "Fibonacci WebSocket client fd=%d already exists, updating", fd);
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
                ESP_LOGI(TAG, "Added Fibonacci WebSocket client: fd=%d, slot=%d", fd, i);
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
                ESP_LOGI(TAG, "Removed Fibonacci WebSocket client: fd=%d, slot=%d", fd, i);
                break;
            }
        }
        xSemaphoreGive(ws_clients_mutex);
    }
}

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
                    ESP_LOGW(TAG, "Ping failed for Fibonacci client fd=%d, slot=%d, cleaning up", ws_clients[i].fd, i);
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                }
                else {
                    active_count++;
                }
            }
        }
        ESP_LOGD(TAG, "Active Fibonacci WebSocket clients: %d", active_count);
        xSemaphoreGive(ws_clients_mutex);
    }
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

    // Broadcast state to WebSocket clients
    fibonacci_broadcast_state();

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t fibonacci_websocket_handler(httpd_req_t* req) {
    int client_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Fibonacci WebSocket connection opened: fd=%d", client_fd);
        add_ws_client(req->handle, client_fd);

        // Send current Fibonacci state immediately upon connection
        cJSON* state_json = create_fibonacci_state_json();
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
                    ESP_LOGW(TAG, "Failed to send initial state to new Fibonacci client fd=%d: %s",
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
            ESP_LOGW(TAG, "Fibonacci WebSocket frame from fd=%d is not properly masked - ignoring frame", client_fd);
            // Don't remove client for masking errors, just ignore this frame
            return ESP_OK;
        }
        else {
            ESP_LOGW(TAG, "Failed to receive Fibonacci WebSocket frame from fd=%d: %s", client_fd, esp_err_to_name(ret));
            remove_ws_client(client_fd);
            return ret;
        }
    }

    if (ws_pkt.len) {
        // Allocate buffer for frame content
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer for Fibonacci WebSocket frame");
            remove_ws_client(client_fd);
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;

        // Receive the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "Fibonacci WebSocket payload from fd=%d is not properly masked - ignoring frame", client_fd);
                free(buf);
                // Don't remove client for masking errors, just ignore this frame
                return ESP_OK;
            }
            else {
                ESP_LOGW(TAG, "Failed to receive Fibonacci WebSocket payload from fd=%d: %s", client_fd, esp_err_to_name(ret));
                free(buf);
                remove_ws_client(client_fd);
                return ret;
            }
        }
    }

    // Handle the frame based on its type
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGD(TAG, "Received Fibonacci WebSocket text from fd=%d: %.*s", client_fd, (int)ws_pkt.len, (char*)ws_pkt.payload);

        cJSON* json = cJSON_Parse((char*)ws_pkt.payload);
        if (json) {
            if (apply_fibonacci_config_from_json(json) == ESP_OK) {
                // Clean up stale connections before broadcasting
                cleanup_stale_clients();
                // Broadcast updated state to all clients
                fibonacci_broadcast_state();
            }
            cJSON_Delete(json);
        }
        else {
            ESP_LOGW(TAG, "Invalid JSON received from Fibonacci WebSocket client fd=%d", client_fd);
        }
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "Fibonacci WebSocket close frame received from fd=%d", client_fd);
        remove_ws_client(client_fd);
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        ESP_LOGD(TAG, "Fibonacci WebSocket pong received from fd=%d", client_fd);
        // Client is alive, no action needed
    }

    if (buf) {
        free(buf);
    }
    return ESP_OK;
}

void fibonacci_broadcast_state(void) {
    init_ws_client_tracking();

    cJSON* state_json = create_fibonacci_state_json();
    if (!state_json) {
        ESP_LOGE(TAG, "Failed to create Fibonacci state JSON for broadcast");
        return;
    }

    char* state_str = cJSON_Print(state_json);
    if (!state_str) {
        cJSON_Delete(state_json);
        ESP_LOGE(TAG, "Failed to serialize Fibonacci state JSON for broadcast");
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
                    ESP_LOGW(TAG, "Failed to send to Fibonacci WebSocket client fd=%d, slot=%d: %s",
                        ws_clients[i].fd, i, esp_err_to_name(ret));
                    // Clean up stale connection immediately
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                    cleaned_count++;
                }
                else {
                    sent_count++;
                    ESP_LOGD(TAG, "Sent state to Fibonacci WebSocket client fd=%d, slot=%d", ws_clients[i].fd, i);
                }
            }
        }

        if (sent_count > 0 || cleaned_count > 0) {
            ESP_LOGD(TAG, "Fibonacci broadcast complete: sent to %d clients, cleaned %d stale connections", sent_count, cleaned_count);
        }

        xSemaphoreGive(ws_clients_mutex);
    }

    free(state_str);
    cJSON_Delete(state_json);
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
    // Register WebSocket handler for real-time Fibonacci control
    httpd_uri_t fibonacci_ws_uri = {
        .uri = "/api/fibonacci/ws",
        .method = HTTP_GET,
        .handler = fibonacci_websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &fibonacci_ws_uri);

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
