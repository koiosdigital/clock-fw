#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(CLOCK_EVENTS);

typedef enum {
    CLOCK_EVENT_MINUTE_TICK,        // Posted every minute change
    CLOCK_EVENT_HOUR_TICK,          // Posted every hour change
    CLOCK_EVENT_CONFIG_CHANGED,     // Posted when clock config changes
    CLOCK_EVENT_FORCE_REFRESH,      // Force immediate display refresh
} clock_event_id_t;

typedef struct {
    int hour;
    int minute;
    int second;
} clock_time_event_data_t;

#ifdef __cplusplus
}
#endif
