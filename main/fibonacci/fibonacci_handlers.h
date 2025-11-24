#pragma once

#include "esp_http_server.h"
#include "fibonacci.h"

/**
 * @brief Register all Fibonacci clock HTTP handlers
 *
 * @param server HTTP server handle
 */
void register_fibonacci_handlers(httpd_handle_t server);

/**
 * @brief HTTP GET handler for Fibonacci configuration
 *
 * Returns current Fibonacci configuration as JSON.
 *
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fibonacci_config_get_handler(httpd_req_t* req);

/**
 * @brief HTTP POST handler for Fibonacci configuration
 *
 * Updates Fibonacci configuration from JSON payload.
 *
 * @param req HTTP request
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fibonacci_config_post_handler(httpd_req_t* req);

/**
 * @brief Load Fibonacci configuration from NVS
 *
 * @param config Pointer to configuration structure to fill
 */
void fibonacci_load_from_nvs(fibonacci_config_t* config);

/**
 * @brief Save Fibonacci configuration to NVS
 *
 * @param config Pointer to configuration structure to save
 */
void fibonacci_save_to_nvs(fibonacci_config_t* config);
