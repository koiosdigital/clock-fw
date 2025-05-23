#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"

#include "kd_common.h"
#include "uart.h"
#include "gps.h"
#include "modbus.h"

extern "C" void app_main(void)
{
    //event loop
    esp_event_loop_create_default();

    //use protocomm security version 0, crypto is disabled in cmakelists
    kd_common_set_provisioning_pop_token_format(ProvisioningPOPTokenFormat_t::NONE);
    kd_common_init();

    uart_init();
    gps_init();
    modbus_init();

    vTaskSuspend(NULL);
}
