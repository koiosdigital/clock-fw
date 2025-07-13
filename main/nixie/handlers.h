#pragma once

#include "esp_http_server.h"

// Route registration for nixie variant
void register_api_handlers(httpd_handle_t server);
