#pragma once

#include "esp_http_server.h"

// Common LED configuration handlers
esp_err_t led_config_get_handler(httpd_req_t* req);
esp_err_t led_config_post_handler(httpd_req_t* req);

// Helper function to register LED handlers
void register_led_handlers(httpd_handle_t server);
