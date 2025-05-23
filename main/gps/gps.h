#pragma once

typedef struct GPSData_t {
    float latitude;         /*!< Latitude */
    float longitude;        /*!< Longitude */
    float altitude;         /*!< Altitude */
} GPSData_t;

void gps_init();
GPSData_t* gps_get_data();

