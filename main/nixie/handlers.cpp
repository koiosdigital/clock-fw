#include "handlers.h"
#include "../api/led_handlers.h"
#include "nixie_handlers.h"
#include "esp_log.h"

void register_api_handlers(httpd_handle_t server) {
    // Register common LED handlers
    register_led_handlers(server);

    // Register nixie handlers
    register_nixie_handlers(server);
}
