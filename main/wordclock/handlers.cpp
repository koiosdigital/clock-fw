#include "handlers.h"
#include "led_handlers.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "API_HANDLERS";

void register_api_handlers(httpd_handle_t server) {
    // Register common LED handlers
    register_led_handlers(server);
}
