#include "internet_time.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "cJSON.h"
#include "kd_common.h"
#include "embedded_tz_db.h"

static const char* TAG = "internet_time";

char http_response_data[512] = { 0 };
esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        memcpy(http_response_data, evt->data, evt->data_len);
        http_response_data[evt->data_len] = '\0';
    }
    return ESP_OK;
}

void setup_time_task(void* pvParameter) {
    while (true) {
        if (kd_common_is_wifi_connected() == false) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else {
            break;
        }
    }

    ESP_LOGI(TAG, "setting up time");

    esp_http_client_config_t config = {
        .url = TIME_INFO_URL,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    esp_http_client_perform(http_client);

    ESP_LOGI(TAG, "response: %s", http_response_data);
    esp_http_client_cleanup(http_client);

    cJSON* root = cJSON_Parse(http_response_data);
    memset(http_response_data, 0, sizeof(http_response_data));

    const char* tzname = cJSON_GetObjectItem(root, "tzname")->valuestring;
    ESP_LOGI(TAG, "timezone: %s", tzname);

    const char* posixTZ = tz_db_get_posix_str(tzname);
    ESP_LOGI(TAG, "converted to POSIX: %s", posixTZ);

    setenv("TZ", posixTZ, 1);
    tzset();

    cJSON_Delete(root);

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "failed to sync time");
        esp_restart();
    }

    vTaskDelete(NULL);
}

void time_init() {
    ESP_LOGI(TAG, "initializing time");

    // Set up the task to fetch and set the time
    xTaskCreate(setup_time_task, "setup_time_task", 4096, NULL, 5, NULL);
}