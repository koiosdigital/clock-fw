#pragma once

#include "esp_http_server.h"

// Handler function declarations
esp_err_t led_config_get_handler(httpd_req_t* req);
esp_err_t led_config_post_handler(httpd_req_t* req);
esp_err_t nixie_config_get_handler(httpd_req_t* req);
esp_err_t nixie_config_post_handler(httpd_req_t* req);

// Route registration
void register_api_handlers(httpd_handle_t server);
