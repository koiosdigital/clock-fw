#pragma once

#include "esp_http_server.h"
#include <stdint.h>

// Nixie configuration structure
typedef struct {
    uint8_t brightness;      // 0-100 scale (maps to 20-100% duty cycle inverted)
    bool military_time;      // true for 24h, false for 12h format
    bool blinking_dots;      // true to blink separator dots with seconds
    bool on;                 // true if nixies are currently on
} nixie_config_t;

// Common nixie configuration handlers
esp_err_t nixie_config_get_handler(httpd_req_t* req);
esp_err_t nixie_config_post_handler(httpd_req_t* req);

// Helper function to register nixie handlers
void register_nixie_handlers(httpd_handle_t server);

// Configuration management functions
void nixie_load_from_nvs(nixie_config_t* config);
void nixie_save_to_nvs(nixie_config_t* config);

// Forward declaration - implemented in clock.cpp
void nixie_load_and_apply_config();
