#include "handlers.h"
#include "../api/led_handlers.h"
#include "../fibonacci_handlers.h"
#include "esp_log.h"

static const char* TAG = "fibonacci_handlers";

void register_api_handlers(httpd_handle_t server) {
    // Register common LED handlers
    register_led_handlers(server);

    // Register fibonacci handlers
    register_fibonacci_handlers(server);

    ESP_LOGI(TAG, "Fibonacci API handlers registered");
}
