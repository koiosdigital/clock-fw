#include "api.h"

#include "mdns.h"
#include "esp_http_server.h"
#include "kd_common.h"

/* Empty handle to esp_http_server */
httpd_handle_t kd_server = NULL;

httpd_handle_t get_httpd_handle() {
    return kd_server;
}

void init_mdns() {
    mdns_init();
    const char* hostname = kd_common_get_device_name();
    mdns_hostname_set(hostname);

    mdns_txt_item_t serviceTxtData[1] = {
        {"model", FIRMWARE_VARIANT},
    };

    ESP_ERROR_CHECK(mdns_service_add(NULL, "_kdclock", "_tcp", 80, serviceTxtData, 3));
}

void server_init() {
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&kd_server, &config);
}

esp_err_t root_handler(httpd_req_t* req) {
    // Handle root requests
    const char* response = "Welcome to the KD Clock API!";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

void api_init() {
    init_mdns();
    server_init();

    httpd_handle_t server = get_httpd_handle();
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);
}