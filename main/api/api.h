#pragma once

#include "esp_http_server.h"

httpd_handle_t get_httpd_handle();

//renamed bc of conflict with esp-idf's mdns
void init_mdns();

void api_init();