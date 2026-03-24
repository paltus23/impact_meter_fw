#include "accel.h"

#include "adxl375.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ACCEL_TASK_STACK_WORDS 4096
#define ACCEL_TASK_PRIORITY 5

#define ADXL375_HOST SPI2_HOST
#define ADXL375_SPI_HZ (5 * 1000 * 1000)
#define ADXL375_PIN_CLK 6
#define ADXL375_PIN_MOSI 7
#define ADXL375_PIN_MISO 2
#define ADXL375_PIN_CS 10

#define ADXL375_INT1_PIN 5
#define ADXL375_INT2_PIN 1

#define ADXL375_FIFO_WATERMARK_SAMPLES 16
#define ADXL375_FIFO_READ_BUFFER_SAMPLES 32

static adxl375_handle_t s_adxl;
static TaskHandle_t s_accel_task_handle;

char TAG[] = "accel";

static void IRAM_ATTR adxl375_int1_isr(void *arg)
{
    (void)arg;
    BaseType_t high_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_accel_task_handle, &high_task_woken);
    if (high_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void accel_task(void *arg)
{
    (void)arg;
    adxl375_sample_t fifo_samples[ADXL375_FIFO_READ_BUFFER_SAMPLES] = {0};

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t int_source = 0;
        if (adxl375_read_int_source(s_adxl, &int_source) != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to read interrupt source");
            continue;
        }

        if ((int_source & ADXL375_INT_WATERMARK) == 0U)
        {
            continue;
        }

        size_t read_samples = 0;
        if (adxl375_read_fifo_samples(s_adxl, fifo_samples, ADXL375_FIFO_READ_BUFFER_SAMPLES, &read_samples) != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to read FIFO");
            continue;
        }

        for (size_t i = 0; i < read_samples; ++i)
        {
            ESP_LOGI(TAG, "fifo[%u] x: %5d y: %5d z: %5d",
                     (unsigned int)i, fifo_samples[i].x, fifo_samples[i].y, fifo_samples[i].z);
        }
        // vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void accel_gpio_interrupt_init(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << ADXL375_INT1_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ADXL375_INT1_PIN, adxl375_int1_isr, NULL));
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
    ESP_ERROR_CHECK(adxl375_check_id(s_adxl));
    ESP_ERROR_CHECK(adxl375_configure(s_adxl, ADXL375_BW_RATE_DEFAULT,
                                      ADXL375_POWER_CTL_MEASURE));
    ESP_ERROR_CHECK(adxl375_data_format(s_adxl, false, false, false, false));

    ESP_ERROR_CHECK(adxl375_fifo_configure(s_adxl, ADXL375_FIFO_MODE_BYPASS, ADXL375_FIFO_WATERMARK_SAMPLES, false));




    uint8_t int_source = 0;
    adxl375_read_int_source(s_adxl, &int_source);

    accel_gpio_interrupt_init();

    xTaskCreate(accel_task, "accel", ACCEL_TASK_STACK_WORDS, NULL,
                ACCEL_TASK_PRIORITY, &s_accel_task_handle);

    ESP_ERROR_CHECK(adxl375_interrupt_configure(s_adxl, 0, 0x00));
    ESP_ERROR_CHECK(adxl375_interrupt_configure(s_adxl, ADXL375_INT_WATERMARK , 0x00));
    ESP_ERROR_CHECK(adxl375_fifo_configure(s_adxl, ADXL375_FIFO_MODE_STREAM, ADXL375_FIFO_WATERMARK_SAMPLES, false));



}
