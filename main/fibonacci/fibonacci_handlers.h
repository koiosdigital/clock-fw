#pragma once

#include "esp_http_server.h"
#include <stdint.h>

// Fibonacci configuration structure
typedef struct {
    uint8_t brightness;     // 0-255 brightness level
    uint8_t theme_id;       // Theme ID
    bool on;                // true if fibonacci display is currently on
} fibonacci_config_t;

// Common fibonacci configuration handlers
esp_err_t fibonacci_config_get_handler(httpd_req_t* req);
esp_err_t fibonacci_config_post_handler(httpd_req_t* req);
esp_err_t fibonacci_themes_handler(httpd_req_t* req);

// Helper function to register fibonacci handlers
void register_fibonacci_handlers(httpd_handle_t server);

// Configuration management functions
void fibonacci_load_from_nvs(fibonacci_config_t* config);
void fibonacci_save_to_nvs(fibonacci_config_t* config);

// Forward declaration - implemented in clock.cpp
void fibonacci_apply_config(fibonacci_config_t* config);
