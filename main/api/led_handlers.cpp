#include "led_handlers.h"
#include "led.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include "api.h"  // For set_cors_headers function
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "led_handlers";

// Structure to track WebSocket connections
typedef struct {
    httpd_handle_t server;
    int fd;
    bool active;
} led_ws_client_t;

#define MAX_WS_CLIENTS 5
static led_ws_client_t ws_clients[MAX_WS_CLIENTS];
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

        ESP_LOGI(TAG, "WebSocket client tracking initialized");
    }
}

// Add a WebSocket client
static void add_ws_client(httpd_handle_t server, int fd) {
    init_ws_client_tracking();

    if (xSemaphoreTake(ws_clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // First, check if this fd already exists (shouldn't happen, but be safe)
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (ws_clients[i].active && ws_clients[i].fd == fd) {
                ESP_LOGW(TAG, "WebSocket client fd=%d already exists, updating", fd);
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
                ESP_LOGI(TAG, "Added WebSocket client: fd=%d, slot=%d", fd, i);
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
                ESP_LOGI(TAG, "Removed WebSocket client: fd=%d, slot=%d", fd, i);
                break;
            }
        }
        xSemaphoreGive(ws_clients_mutex);
    }
}

// Helper function to create LED state JSON
static cJSON* create_led_state_json(void) {
    led_persistent_config_t config;
    led_load_from_nvs(&config);

    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
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

    return json;
}

// Helper function to apply LED configuration from JSON
static esp_err_t apply_led_config_from_json(cJSON* json) {
    if (!json) return ESP_FAIL;

    // Get fields
    cJSON* mode_json = cJSON_GetObjectItem(json, "mode");
    cJSON* color_json = cJSON_GetObjectItem(json, "color");
    cJSON* brightness_json = cJSON_GetObjectItem(json, "brightness");
    cJSON* speed_json = cJSON_GetObjectItem(json, "speed");
    cJSON* on_json = cJSON_GetObjectItem(json, "on");

    led_persistent_config_t config;
    led_load_from_nvs(&config);

    // Validate mode
    if (cJSON_IsString(mode_json)) {
        const char* mode_str = cJSON_GetStringValue(mode_json);
        LEDEffect_t new_effect = LED_OFF;

        if (strcmp(mode_str, "off") == 0) new_effect = LED_OFF;
        else if (strcmp(mode_str, "solid") == 0) new_effect = LED_SOLID;
        else if (strcmp(mode_str, "blink") == 0) new_effect = LED_BLINK;
        else if (strcmp(mode_str, "breathe") == 0) new_effect = LED_BREATHE;
        else if (strcmp(mode_str, "cyclic") == 0) new_effect = LED_CYCLIC;
        else if (strcmp(mode_str, "rainbow") == 0) new_effect = LED_RAINBOW;
        else {
            ESP_LOGW(TAG, "Invalid mode: %s", mode_str);
            return ESP_FAIL;
        }

        led_set_effect(new_effect);
        config.effect = new_effect;
    }

    // Validate color if present
    if (cJSON_IsObject(color_json)) {
        cJSON* red_json = cJSON_GetObjectItem(color_json, "r");
        cJSON* green_json = cJSON_GetObjectItem(color_json, "g");
        cJSON* blue_json = cJSON_GetObjectItem(color_json, "b");
        cJSON* white_json = cJSON_GetObjectItem(color_json, "w");

        uint8_t r = config.r, g = config.g, b = config.b, w = config.w;

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
    if (cJSON_IsNumber(brightness_json)) {
        int val = cJSON_GetNumberValue(brightness_json);
        uint8_t brightness = (val < 0) ? 0 : (val > 255) ? 255 : val;
        led_set_brightness(brightness);
        config.brightness = brightness;
    }

    // Validate speed if present
    if (cJSON_IsNumber(speed_json)) {
        int val = cJSON_GetNumberValue(speed_json);
        uint8_t speed = (val < 1) ? 1 : (val > 100) ? 100 : val;
        led_set_speed(speed);
        config.speed = speed;
    }

    // Validate on state if present
    if (cJSON_IsBool(on_json)) {
        bool on = cJSON_IsTrue(on_json);
        led_set_on_state(on);
        config.on = on;
    }

    led_save_to_nvs(&config);
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
                    ESP_LOGW(TAG, "Ping failed for client fd=%d, slot=%d, cleaning up", ws_clients[i].fd, i);
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                }
                else {
                    active_count++;
                }
            }
        }
        ESP_LOGD(TAG, "Active WebSocket clients: %d", active_count);
        xSemaphoreGive(ws_clients_mutex);
    }
}

// Legacy HTTP handlers for backward compatibility
esp_err_t led_config_get_handler(httpd_req_t* req) {
    cJSON* json = create_led_state_json();
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    // Apply configuration using shared function
    esp_err_t result = apply_led_config_from_json(json);
    cJSON_Delete(json);

    if (result != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return ESP_FAIL;
    }

    // Broadcast state to WebSocket clients
    led_broadcast_state();

    // Return success response
    const char* response = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

esp_err_t led_websocket_handler(httpd_req_t* req) {
    int client_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket connection opened: fd=%d", client_fd);
        add_ws_client(req->handle, client_fd);

        // Send current LED state immediately upon connection
        cJSON* state_json = create_led_state_json();
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
                    ESP_LOGW(TAG, "Failed to send initial state to new client fd=%d: %s",
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
            ESP_LOGW(TAG, "WebSocket frame from fd=%d is not properly masked - ignoring frame", client_fd);
            // Don't remove client for masking errors, just ignore this frame
            return ESP_OK;
        }
        else {
            ESP_LOGW(TAG, "Failed to receive WebSocket frame from fd=%d: %s", client_fd, esp_err_to_name(ret));
            remove_ws_client(client_fd);
            return ret;
        }
    }

    if (ws_pkt.len) {
        // Allocate buffer for frame content
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer for WebSocket frame");
            remove_ws_client(client_fd);
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;

        // Receive the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "WebSocket payload from fd=%d is not properly masked - ignoring frame", client_fd);
                free(buf);
                // Don't remove client for masking errors, just ignore this frame
                return ESP_OK;
            }
            else {
                ESP_LOGW(TAG, "Failed to receive WebSocket payload from fd=%d: %s", client_fd, esp_err_to_name(ret));
                free(buf);
                remove_ws_client(client_fd);
                return ret;
            }
        }
    }

    // Handle the frame based on its type
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGD(TAG, "Received WebSocket text from fd=%d: %.*s", client_fd, (int)ws_pkt.len, (char*)ws_pkt.payload);

        cJSON* json = cJSON_Parse((char*)ws_pkt.payload);
        if (json) {
            if (apply_led_config_from_json(json) == ESP_OK) {
                // Clean up stale connections before broadcasting
                cleanup_stale_clients();
                // Broadcast updated state to all clients
                led_broadcast_state();
            }
            cJSON_Delete(json);
        }
        else {
            ESP_LOGW(TAG, "Invalid JSON received from WebSocket client fd=%d", client_fd);
        }
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket close frame received from fd=%d", client_fd);
        remove_ws_client(client_fd);
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        ESP_LOGD(TAG, "WebSocket pong received from fd=%d", client_fd);
        // Client is alive, no action needed
    }

    if (buf) {
        free(buf);
    }
    return ESP_OK;
}

void led_broadcast_state(void) {
    init_ws_client_tracking();

    cJSON* state_json = create_led_state_json();
    if (!state_json) {
        ESP_LOGE(TAG, "Failed to create LED state JSON for broadcast");
        return;
    }

    char* state_str = cJSON_Print(state_json);
    if (!state_str) {
        cJSON_Delete(state_json);
        ESP_LOGE(TAG, "Failed to serialize LED state JSON for broadcast");
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
                    ESP_LOGW(TAG, "Failed to send to WebSocket client fd=%d, slot=%d: %s",
                        ws_clients[i].fd, i, esp_err_to_name(ret));
                    // Clean up stale connection immediately
                    ws_clients[i].active = false;
                    ws_clients[i].fd = -1;
                    ws_clients[i].server = NULL;
                    cleaned_count++;
                }
                else {
                    sent_count++;
                    ESP_LOGD(TAG, "Sent state to WebSocket client fd=%d, slot=%d", ws_clients[i].fd, i);
                }
            }
        }

        if (sent_count > 0 || cleaned_count > 0) {
            ESP_LOGD(TAG, "Broadcast complete: sent to %d clients, cleaned %d stale connections", sent_count, cleaned_count);
        }

        xSemaphoreGive(ws_clients_mutex);
    }

    free(state_str);
    cJSON_Delete(state_json);
}

void register_led_handlers(httpd_handle_t server) {
    // Register WebSocket handler for real-time LED control
    httpd_uri_t led_ws_uri = {
        .uri = "/api/leds/ws",
        .method = HTTP_GET,
        .handler = led_websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &led_ws_uri);

    // Register legacy HTTP handlers for backward compatibility
    httpd_uri_t led_config_get_uri = {
        .uri = "/api/leds",
        .method = HTTP_GET,
        .handler = led_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &led_config_get_uri);

    httpd_uri_t led_config_post_uri = {
        .uri = "/api/leds",
        .method = HTTP_POST,
        .handler = led_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &led_config_post_uri);
}
