#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include <esp_wifi.h>
#include "wifi_provisioning/manager.h"
#include "protocomm_ble.h"

#include "kd_common.h"
#include "cJSON.h"
#include "api.h"
#include "kd_pixdriver.h"
#include "clock_time_ticker.h"

static TaskHandle_t s_clock_task_handle = NULL;

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE
#include "nixie/nixie.h"
#elif CONFIG_BASE_CLOCK_TYPE_FIBONACCI
#include "fibonacci/fibonacci.h"
#elif CONFIG_BASE_CLOCK_TYPE_WORDCLOCK
#include "wordclock/wordclock.h"
#else
#error "No base clock type selected"
#endif

void wifi_prov_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi Provisioning connected");
    PixelDriver::getMainChannel()->setColor(PixelColor(0, 0, 255));
    PixelDriver::getMainChannel()->setEffectByID("SOLID");
}

void wifi_prov_started(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi Provisioning started");
    PixelDriver::getMainChannel()->setColor(PixelColor(0, 0, 255));
    PixelDriver::getMainChannel()->setEffectByID("BREATHE");
}

void wifi_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi disconnected - waiting for connection");
    PixelDriver::getMainChannel()->setColor(PixelColor(0, 255, 255));
    PixelDriver::getMainChannel()->setEffectByID("BREATHE");
}

void wifi_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi connected - starting time sync");
    PixelDriver::getMainChannel()->setColor(PixelColor(255, 255, 0));
    PixelDriver::getMainChannel()->setEffectByID("CYCLIC");

    // Only create clock task once (prevent duplicates on WiFi reconnect)
    if (s_clock_task_handle == NULL) {
#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE
        xTaskCreate(nixie_clock_task, "clock_task", 2560, NULL, 5, &s_clock_task_handle);
#elif CONFIG_BASE_CLOCK_TYPE_FIBONACCI
        xTaskCreate(fibonacci_clock_task, "clock_task", 2560, NULL, 5, &s_clock_task_handle);
#elif CONFIG_BASE_CLOCK_TYPE_WORDCLOCK
        xTaskCreate(wordclock_clock_task, "clock_task", 2560, NULL, 5, &s_clock_task_handle);
#else
#error "No base clock type selected"
#endif
    }
}

extern "C" void app_main(void)
{
    //event loop
    esp_event_loop_create_default();

    //use protocomm security version 0
    kd_common_set_provisioning_pop_token_format(ProvisioningPOPTokenFormat_t::NONE);
    kd_common_init();

    // Initialize time ticker (posts CLOCK_EVENT_MINUTE_TICK and CLOCK_EVENT_HOUR_TICK)
    clock_time_ticker_init();

    api_init();

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE
    nixie_clock_init();
#elif CONFIG_BASE_CLOCK_TYPE_FIBONACCI
    fibonacci_clock_init();
#elif CONFIG_BASE_CLOCK_TYPE_WORDCLOCK
    wordclock_clock_init();
#else
#error "No base clock type selected"
#endif

    esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, PROTOCOMM_TRANSPORT_BLE_CONNECTED, &wifi_prov_connected, NULL);
    esp_event_handler_register(WIFI_PROV_EVENT, WIFI_PROV_START, &wifi_prov_started, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_disconnected, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_connected, NULL);
}
