#include "accel.h"

#include "adxl375.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define ACCEL_TASK_STACK_WORDS 4096
#define ACCEL_TASK_PRIORITY 5

#define ADXL375_HOST SPI2_HOST
#define ADXL375_SPI_HZ (2 * 1000 * 1000)
#define ADXL375_PIN_CLK 6
#define ADXL375_PIN_MOSI 7
#define ADXL375_PIN_MISO 2
#define ADXL375_PIN_CS 10

static adxl375_handle_t s_adxl;

char TAG[] = "accel";

static void accel_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        int16_t x = 0, y = 0, z = 0;
        adxl375_read_xyz(s_adxl, &x, &y, &z);
        ESP_LOGI(TAG, "x: %5d y: %5d z: %5d", x, y, z);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void accel_init(void)
{
    adxl375_spi_config_t adxl_cfg = {
        .host = ADXL375_HOST,
        .mosi_io = ADXL375_PIN_MOSI,
        .miso_io = ADXL375_PIN_MISO,
        .sclk_io = ADXL375_PIN_CLK,
        .cs_io = ADXL375_PIN_CS,
        .clock_hz = ADXL375_SPI_HZ,
        .free_bus_on_deinit = true,
    };

    ESP_ERROR_CHECK(adxl375_spi_init(&adxl_cfg, &s_adxl));
    adxl375_check_id(s_adxl);
    adxl375_configure(s_adxl, ADXL375_BW_RATE_DEFAULT,
                      ADXL375_POWER_CTL_MEASURE);

    xTaskCreate(accel_task, "accel", ACCEL_TASK_STACK_WORDS, NULL,
                ACCEL_TASK_PRIORITY, NULL);
}
