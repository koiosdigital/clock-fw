#include "api.h"

#include "kd_common.h"
#include "kd_pixdriver.h"
#include "static_files.h"

#include <esp_http_server.h>
#include <string.h>

// Static file handler function
static esp_err_t static_file_handler(httpd_req_t* req) {
    const static_files::file* f = reinterpret_cast<const static_files::file*>(req->user_ctx);
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Set appropriate headers
    httpd_resp_set_type(req, f->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    // Add caching headers for static assets (except HTML)
    if (strcmp(f->type, "text/html") != 0) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000"); // 1 year
    }
    else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache"); // Don't cache HTML
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "X-XSS-Protection", "1; mode=block");
    }

    httpd_resp_send(req, reinterpret_cast<const char*>(f->contents), f->size);
    return ESP_OK;
}

static void register_clock_handlers(httpd_handle_t server) {
    // Register PixelDriver API endpoints
    PixelDriver::attach_api(server);

    // Create an array of httpd_uri_t to keep them alive after the loop
    static httpd_uri_t static_file_uris[static_files::num_of_files + 1]; // +1 for root '/' override

    // Register static files
    for (int i = 0; i < static_files::num_of_files; i++) {
        const static_files::file& f = static_files::files[i];

        static_file_uris[i] = {
            .uri = f.path,
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = (void*)&static_files::files[i]
        };

        httpd_register_uri_handler(server, &static_file_uris[i]);
    }

    // Find index.html file to serve at root '/'
    const static_files::file* index_file = nullptr;
    for (int i = 0; i < static_files::num_of_files; i++) {
        if (strcmp(static_files::files[i].path, "/index.html") == 0) {
            index_file = &static_files::files[i];
            break;
        }
    }

    if (index_file) {
        // Override root URI handler '/' to serve index.html instead of welcome message
        static_file_uris[static_files::num_of_files] = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = (void*)index_file
        };
        httpd_register_uri_handler(server, &static_file_uris[static_files::num_of_files]);
    }
}

//subtype
#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE
static const char* subtype = "nixie";
#elif CONFIG_BASE_CLOCK_TYPE_FIBONACCI
static const char* subtype = "fibonacci";
#elif CONFIG_BASE_CLOCK_TYPE_WORDCLOCK
static const char* subtype = "wordclock";
#else
#error "No base clock type selected"
#endif

void clock_api_init() {
    // Set device info for mDNS (model, type)
    // Note: subtype is clock-specific but mDNS is handled by kd_common
    kd_common_set_device_info(FIRMWARE_VARIANT, subtype);

    // Register handler callback - will be called when httpd starts (on WiFi connect)
    kd_common_api_register_handlers(register_clock_handlers);
}
