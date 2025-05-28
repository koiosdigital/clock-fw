#include "gps.h"

#include "nmea_parser.h"

#include "esp_log.h"

GPSData_t gps_data = {
    .latitude = 0.0,
    .longitude = 0.0,
    .altitude = 0.0
};

static const char* TAG = "gps";

static void gps_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    gps_t* gps = NULL;
    switch (event_id) {
    case GPS_UPDATE:
        gps = (gps_t*)event_data;
        ESP_LOGI(TAG, "GPS update: lat:%.6f lon:%.6f alt:%.2f", gps->latitude, gps->longitude, gps->altitude);
        gps_data.latitude = gps->latitude;
        gps_data.longitude = gps->longitude;
        gps_data.altitude = gps->altitude;
        break;
    case GPS_UNKNOWN:
        ESP_LOGW(TAG, "Unknown statement:%s", (char*)event_data);
        break;
    default:
        break;
    }
}

GPSData_t* gps_get_data()
{
    return &gps_data;
}

void gps_init()
{
#if HW_TYPE == HW_TYPE_ROVER
    /* NMEA parser configuration */
    nmea_parser_config_t config = {
        .uart = {
            .uart_port = UART_NUM_1,
            .rx_pin = 17,
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .event_queue_size = 16
        }
    };
    /* init NMEA parser library */
    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);
    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
#endif
}