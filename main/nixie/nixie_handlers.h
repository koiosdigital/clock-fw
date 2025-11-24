#pragma once

#include "esp_http_server.h"
#include <stdint.h>
#include <stdbool.h>
#include "nixie.h"

// Helper function to register nixie handlers
void register_nixie_handlers(httpd_handle_t server);

// Legacy HTTP handlers for backward compatibility
esp_err_t nixie_config_get_handler(httpd_req_t* req);
esp_err_t nixie_config_post_handler(httpd_req_t* req);

// NVS functions
void nixie_load_from_nvs(nixie_config_t* config);
void nixie_save_to_nvs(const nixie_config_t* config);

// Configuration getters/setters
nixie_config_t nixie_get_config(void);
void nixie_set_config(const nixie_config_t* config);
void nixie_apply_config(const nixie_config_t* config);
