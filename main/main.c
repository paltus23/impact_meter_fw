#include <stdio.h>
#include "accel.h"
#include "battery_measure.h"
#include "data_storage.h"
#include "http_server.h"
#include "led.h"
#include "settings.h"
#include "wifi.h"

void app_main(void)
{
    settings_init();
    wifi_init();
    data_storage_init();
    battery_measure_init();
    led_init();
    accel_init();
    http_server_init();
}
