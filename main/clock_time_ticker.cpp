#include "clock_time_ticker.h"
#include "clock_events.h"
#include "kd_common.h"

#include <esp_timer.h>
#include <esp_log.h>
#include <esp_event.h>
#include <time.h>

static const char* TAG = "clock_ticker";

namespace {

esp_timer_handle_t g_ticker_timer = nullptr;
bool g_running = false;
int g_last_minute = -1;
int g_last_hour = -1;

void ticker_callback(void* arg) {
    if (!kd_common_ntp_is_synced()) {
        return;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    clock_time_event_data_t event_data = {
        .hour = timeinfo.tm_hour,
        .minute = timeinfo.tm_min,
        .second = timeinfo.tm_sec
    };

    // Check for minute change
    if (timeinfo.tm_min != g_last_minute) {
        g_last_minute = timeinfo.tm_min;

        // Post minute tick event
        esp_event_post(CLOCK_EVENTS, CLOCK_EVENT_MINUTE_TICK,
                       &event_data, sizeof(event_data), 0);

        ESP_LOGD(TAG, "Minute tick: %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

        // Check for hour change
        if (timeinfo.tm_hour != g_last_hour) {
            g_last_hour = timeinfo.tm_hour;

            // Post hour tick event
            esp_event_post(CLOCK_EVENTS, CLOCK_EVENT_HOUR_TICK,
                           &event_data, sizeof(event_data), 0);

            ESP_LOGI(TAG, "Hour tick: %02d:00", timeinfo.tm_hour);
        }
    }
}

void on_ntp_sync(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == 0) {  // KD_NTP_EVENT_SYNC_COMPLETE
        ESP_LOGI(TAG, "NTP synced, starting time ticker");
        clock_time_ticker_start();

        // Post initial minute tick to trigger immediate display update
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        clock_time_event_data_t event_data = {
            .hour = timeinfo.tm_hour,
            .minute = timeinfo.tm_min,
            .second = timeinfo.tm_sec
        };

        g_last_minute = timeinfo.tm_min;
        g_last_hour = timeinfo.tm_hour;

        esp_event_post(CLOCK_EVENTS, CLOCK_EVENT_MINUTE_TICK,
                       &event_data, sizeof(event_data), 0);
    }
    else if (id == 1) {  // KD_NTP_EVENT_SYNC_LOST
        ESP_LOGW(TAG, "NTP sync lost, stopping time ticker");
        clock_time_ticker_stop();
    }
}

}  // namespace

void clock_time_ticker_init() {
    if (g_ticker_timer != nullptr) {
        return;  // Already initialized
    }

    // Create timer (500ms interval for responsive minute detection)
    esp_timer_create_args_t timer_args = {
        .callback = ticker_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_ticker",
        .skip_unhandled_events = true
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_ticker_timer));

    // Register for NTP events
    ESP_ERROR_CHECK(esp_event_handler_register(
        KD_NTP_EVENTS, ESP_EVENT_ANY_ID, on_ntp_sync, nullptr));

    // If already synced, start immediately
    if (kd_common_ntp_is_synced()) {
        clock_time_ticker_start();

        // Post initial event
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        clock_time_event_data_t event_data = {
            .hour = timeinfo.tm_hour,
            .minute = timeinfo.tm_min,
            .second = timeinfo.tm_sec
        };

        g_last_minute = timeinfo.tm_min;
        g_last_hour = timeinfo.tm_hour;

        esp_event_post(CLOCK_EVENTS, CLOCK_EVENT_MINUTE_TICK,
                       &event_data, sizeof(event_data), 0);
    }

    ESP_LOGI(TAG, "Time ticker initialized");
}

void clock_time_ticker_start() {
    if (g_running || g_ticker_timer == nullptr) {
        return;
    }

    // Reset tracking
    g_last_minute = -1;
    g_last_hour = -1;

    // Start timer (500ms interval)
    ESP_ERROR_CHECK(esp_timer_start_periodic(g_ticker_timer, 500 * 1000));
    g_running = true;

    ESP_LOGI(TAG, "Time ticker started");
}

void clock_time_ticker_stop() {
    if (!g_running || g_ticker_timer == nullptr) {
        return;
    }

    esp_timer_stop(g_ticker_timer);
    g_running = false;

    ESP_LOGI(TAG, "Time ticker stopped");
}

bool clock_time_ticker_is_running() {
    return g_running;
}
