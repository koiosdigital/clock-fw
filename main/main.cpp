#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"

#include "kd_common.h"
#include "cJSON.h"
#include "internet_time.h"
#include "clock.h"

extern "C" void app_main(void)
{
    //event loop
    esp_event_loop_create_default();

    //use protocomm security version 0
    kd_common_set_provisioning_pop_token_format(ProvisioningPOPTokenFormat_t::NONE);
    kd_common_init();

    time_init();
    clock_init();

    vTaskSuspend(NULL);
}
