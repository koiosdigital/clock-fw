#pragma once

#include "esp_http_server.h"

// WebSocket LED configuration handler
esp_err_t led_websocket_handler(httpd_req_t* req);

// Helper function to broadcast LED state to all connected clients
void led_broadcast_state(void);

// Helper function to register LED handlers
void register_led_handlers(httpd_handle_t server);

// Legacy HTTP handlers (can be kept for backward compatibility)
esp_err_t led_config_get_handler(httpd_req_t* req);
esp_err_t led_config_post_handler(httpd_req_t* req);
