#include "accel.h"

#include "adxl375.h"
#include "profile_header.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "data_storage.h"
#include "settings.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

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

#define ACCEL_ODR ADXL375_ODR_3200_HZ

#define ADXL375_FIFO_WATERMARK_SAMPLES 16
#define ADXL375_FIFO_READ_BUFFER_SAMPLES 32

/* Shock detection: threshold = 50 g (50000 mg / 780 mg per LSB ≈ 64),
 * duration = 10 ms (10000 µs / 625 µs per LSB = 16). */
#define ACCEL_SHOCK_THRESHOLD_LSB 10U
#define ACCEL_SHOCK_DURATION_LSB 2U
#define ACCEL_SHOCK_AXES (ADXL375_SHOCK_AXES_X | ADXL375_SHOCK_AXES_Y | ADXL375_SHOCK_AXES_Z)

/* Activity detection: threshold = 5 g (5000 mg / 780 mg per LSB ≈ 6), AC-coupled, all axes. */
#define ACCEL_ACT_THRESHOLD_LSB 6U
#define ACCEL_ACT_CTL (ADXL375_ACT_CTL_AC | ADXL375_ACT_CTL_AXES_X | ADXL375_ACT_CTL_AXES_Y | ADXL375_ACT_CTL_AXES_Z)

/* Ring buffer sizing: upper bound is taken from settings so both layers agree.
 * SETTINGS_MAX_PRECAPTURE_MS=5000 → 500 samples × 6 B = 3 kB of static storage. */
#define ACCEL_RING_BUFFER_SAMPLE_RATE   100
#define ACCEL_RING_BUFFER_MAX_SAMPLES   ((SETTINGS_MAX_PRECAPTURE_MS * ACCEL_RING_BUFFER_SAMPLE_RATE) / 1000)

#define ACCEL_CAPTURE_FILE_NAME_FMT     "capture_%ld.bin"
#define ACCEL_CAPTURE_FILE_NAME_LEN     32

static adxl375_handle_t s_adxl;
static TaskHandle_t s_accel_task_handle;

static const char *TAG = "accel";

/* ------------------------------------------------------------------ runtime settings */

static int32_t s_profile_num   = SETTINGS_DEFAULT_PROFILE_NUM;
static int32_t s_precapture_ms = SETTINGS_DEFAULT_PRECAPTURE_MS;
static int32_t s_capture_ms    = SETTINGS_DEFAULT_CAPTURE_MS;
static char    s_capture_file_name[ACCEL_CAPTURE_FILE_NAME_LEN];

/* ------------------------------------------------------------------ ring buffer */

typedef struct
{
    adxl375_sample_t samples[ACCEL_RING_BUFFER_MAX_SAMPLES];
    size_t head;
    size_t count;
    size_t capacity; /* effective window in samples, <= ACCEL_RING_BUFFER_MAX_SAMPLES */
} accel_ring_buffer_t;

static accel_ring_buffer_t s_pre_event_buffer;

static esp_err_t log_profile(const char *name);
static esp_err_t accel_write_profile_header(data_storage_file_t file);

/* ------------------------------------------------------------------ settings helpers */

static void accel_load_settings(void)
{
    settings_get_profile_num(&s_profile_num);
    settings_get_precapture_ms(&s_precapture_ms);
    settings_get_capture_ms(&s_capture_ms);

    size_t new_cap = (size_t)((s_precapture_ms * ACCEL_RING_BUFFER_SAMPLE_RATE) / 1000);
    if (new_cap == 0)
    {
        new_cap = 1;
    }
    if (new_cap > ACCEL_RING_BUFFER_MAX_SAMPLES)
    {
        new_cap = ACCEL_RING_BUFFER_MAX_SAMPLES;
    }

    s_pre_event_buffer.capacity = new_cap;
    s_pre_event_buffer.head = 0;
    s_pre_event_buffer.count = 0;

    snprintf(s_capture_file_name, sizeof(s_capture_file_name),
             ACCEL_CAPTURE_FILE_NAME_FMT, (long)s_profile_num);

    ESP_LOGI(TAG, "settings: profile=%ld precap=%ld ms cap=%ld ms file='%s'",
             (long)s_profile_num, (long)s_precapture_ms,
             (long)s_capture_ms, s_capture_file_name);
}

/* ------------------------------------------------------------------ ring buffer ops */

static void accel_ring_buffer_write(const adxl375_sample_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0U)
    {
        return;
    }

    for (size_t i = 0; i < sample_count; ++i)
    {
        s_pre_event_buffer.samples[s_pre_event_buffer.head] = samples[i];
        s_pre_event_buffer.head = (s_pre_event_buffer.head + 1U) % s_pre_event_buffer.capacity;
        if (s_pre_event_buffer.count < s_pre_event_buffer.capacity)
        {
            s_pre_event_buffer.count++;
        }
    }
}

static esp_err_t accel_ring_buffer_flush_to_file(data_storage_file_t file)
{
    if (!file)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pre_event_buffer.count == 0U)
    {
        return ESP_OK;
    }

    const size_t cap   = s_pre_event_buffer.capacity;
    const size_t start = (s_pre_event_buffer.head + cap - s_pre_event_buffer.count) % cap;

    const size_t first_part  = cap - start;
    const size_t first_count = (s_pre_event_buffer.count < first_part)
                                   ? s_pre_event_buffer.count
                                   : first_part;
    if (first_count > 0U)
    {
        esp_err_t ret = data_storage_write(file,
                                           &s_pre_event_buffer.samples[start],
                                           first_count * sizeof(adxl375_sample_t));
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    const size_t remaining = s_pre_event_buffer.count - first_count;
    if (remaining > 0U)
    {
        esp_err_t ret = data_storage_write(file,
                                           &s_pre_event_buffer.samples[0],
                                           remaining * sizeof(adxl375_sample_t));
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    s_pre_event_buffer.count = 0U;
    return ESP_OK;
}

TickType_t event_start_tick = 0;
static bool accel_start_event_occurred(void)
{
    /* User-defined trigger hook: replace this condition with your event logic. */

    TickType_t elapsed_ticks = xTaskGetTickCount() - event_start_tick;

    if (elapsed_ticks > pdMS_TO_TICKS(4000))
    {
        event_start_tick = xTaskGetTickCount();
        return true;
    }

    return false;
}

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
    bool event_triggered = false;
    TickType_t capture_start_tick = 0;
    data_storage_file_t capture_file = NULL;
    size_t capture_data_bytes = 0;

    event_start_tick = xTaskGetTickCount() + pdMS_TO_TICKS(3000);

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Reload settings if the HTTP layer pushed a config-changed notification. */
        EventBits_t cfg_bits = xEventGroupGetBits(settings_get_event_group());
        if (cfg_bits & SETTINGS_EVENT_CONFIG_CHANGED)
        {
            xEventGroupClearBits(settings_get_event_group(), SETTINGS_EVENT_CONFIG_CHANGED);
            accel_load_settings();
        }

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

        if (read_samples == 0U)
        {
            continue;
        }

        static TickType_t print_ticks = 0;
        TickType_t elapsed_ticks = xTaskGetTickCount() - print_ticks;
        if (elapsed_ticks >= pdMS_TO_TICKS(5000))
        {
            print_ticks = xTaskGetTickCount();
            ESP_LOGI(TAG, "%d %d %d",
                     fifo_samples[0].x, fifo_samples[0].y, fifo_samples[0].z);
        }
        accel_ring_buffer_write(fifo_samples, read_samples);

        if (!event_triggered)
        {
            if ((int_source & ADXL375_INT_SINGLE_SHOCK) || (int_source & ADXL375_INT_ACTIVITY))
            {
                esp_err_t ret = data_storage_open_profile(s_capture_file_name, &capture_file);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "failed to open capture file '%s': %s",
                             s_capture_file_name, esp_err_to_name(ret));
                    continue;
                }

                ret = accel_write_profile_header(capture_file);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "failed to write profile header: %s", esp_err_to_name(ret));
                    (void)data_storage_close(capture_file);
                    capture_file = NULL;
                    continue;
                }

                size_t pre_event_bytes = s_pre_event_buffer.count * sizeof(adxl375_sample_t);
                ret = accel_ring_buffer_flush_to_file(capture_file);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "failed to flush pre-event samples: %s", esp_err_to_name(ret));
                    (void)data_storage_close(capture_file);
                    capture_file = NULL;
                    continue;
                }

                capture_data_bytes = pre_event_bytes;
                event_triggered = true;
                capture_start_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, "event detected, capturing to '%s'", s_capture_file_name);
            }
        }

        if (event_triggered)
        {
            elapsed_ticks = xTaskGetTickCount() - capture_start_tick;
            if (pdTICKS_TO_MS(elapsed_ticks) >= (uint32_t)s_capture_ms)
            {
                uint32_t ds = (uint32_t)capture_data_bytes;
                esp_err_t patch_ret = data_storage_pwrite(capture_file,
                                                          offsetof(profile_header_t, data_size),
                                                          &ds, sizeof(ds));
                if (patch_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "failed to patch header data_size: %s",
                             esp_err_to_name(patch_ret));
                }
                (void)data_storage_close(capture_file);
                capture_file = NULL;
                capture_data_bytes = 0;
                event_triggered = false;
                ESP_LOGI(TAG, "capture complete ('%s', data=%lu B)", s_capture_file_name, (unsigned long)ds);
                // log_profile(s_capture_file_name);
                s_profile_num++;
                settings_set_profile_num(s_profile_num);
                snprintf(s_capture_file_name, sizeof(s_capture_file_name),
                ACCEL_CAPTURE_FILE_NAME_FMT, (long)s_profile_num);
                continue;
            }

            size_t chunk_bytes = read_samples * sizeof(adxl375_sample_t);
            esp_err_t ret = data_storage_write(capture_file, fifo_samples, chunk_bytes);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "failed to write samples: %s", esp_err_to_name(ret));
            }
            else
            {
                capture_data_bytes += chunk_bytes;
            }
        }
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

static esp_err_t log_profile(const char *name)
{
    if (!name)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t profile_size = 0;
    esp_err_t ret = data_storage_get_profile_size(name, &profile_size);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to get profile '%s' size: %s", name, esp_err_to_name(ret));
        return ret;
    }

    if (profile_size == 0U)
    {
        ESP_LOGI(TAG, "ACCEL_PROFILE name=%s samples=0 (empty)", name);
        return ESP_OK;
    }

    size_t sample_count = profile_size / sizeof(adxl375_sample_t);
    size_t tail_bytes = profile_size % sizeof(adxl375_sample_t);
    if (tail_bytes != 0U)
    {
        ESP_LOGW(TAG, "profile '%s' has %u tail bytes (ignored)", name, (unsigned)tail_bytes);
    }

    ESP_LOGI(TAG, "ACCEL_PROFILE_BEGIN name=%s bytes=%u samples=%u",
             name, (unsigned)profile_size, (unsigned)sample_count);

    adxl375_sample_t chunk[ADXL375_FIFO_READ_BUFFER_SAMPLES] = {0};
    size_t byte_offset = 0U;
    size_t sample_index = 0U;
    while (byte_offset < (sample_count * sizeof(adxl375_sample_t)))
    {
        size_t to_read = sizeof(chunk);
        size_t remaining = (sample_count * sizeof(adxl375_sample_t)) - byte_offset;
        if (to_read > remaining)
        {
            to_read = remaining;
        }

        size_t bytes_read = 0U;
        ret = data_storage_read_profile(name, byte_offset, chunk, to_read, &bytes_read);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "failed to read profile '%s' at offset %u: %s",
                     name, (unsigned)byte_offset, esp_err_to_name(ret));
            return ret;
        }

        if (bytes_read == 0U)
        {
            break;
        }

        size_t samples_read = bytes_read / sizeof(adxl375_sample_t);
        for (size_t i = 0; i < samples_read; ++i)
        {
            printf("idx=%4u %d %d %d\n",
                   (unsigned)sample_index, chunk[i].x, chunk[i].y, chunk[i].z);
            sample_index++;
        }

        byte_offset += bytes_read;
    }

    ESP_LOGI(TAG, "ACCEL_PROFILE_END name=%s printed_samples=%u",
             name, (unsigned)sample_index);
    return ESP_OK;
}

static esp_err_t accel_write_profile_header(data_storage_file_t file)
{
    profile_header_t hdr = {
        .magic       = PROFILE_HEADER_MAGIC,
        .version     = PROFILE_HEADER_VERSION,
        .header_size = (uint16_t)sizeof(profile_header_t),
        .odr_hz      = adxl375_odr_to_hz(ACCEL_ODR),
        .data_size   = 0U,
    };
    return data_storage_write(file, &hdr, sizeof(hdr));
}

void accel_init(void)
{
    accel_load_settings();

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
    ESP_ERROR_CHECK(adxl375_configure(s_adxl, ACCEL_ODR,
                                      ADXL375_POWER_CTL_MEASURE));
    ESP_ERROR_CHECK(adxl375_data_format(s_adxl, false, false, false, false));

    ESP_ERROR_CHECK(adxl375_fifo_configure(s_adxl, ADXL375_FIFO_MODE_BYPASS, ADXL375_FIFO_WATERMARK_SAMPLES, false));

    // ESP_ERROR_CHECK(adxl375_shock_configure(s_adxl,
    //                                         ACCEL_SHOCK_THRESHOLD_LSB,
    //                                         ACCEL_SHOCK_DURATION_LSB,
    //                                         ACCEL_SHOCK_AXES));
    ESP_ERROR_CHECK(adxl375_act_configure(s_adxl,
                                          ACCEL_ACT_THRESHOLD_LSB,
                                          ACCEL_ACT_CTL));

    uint8_t int_source = 0;
    adxl375_read_int_source(s_adxl, &int_source);

    accel_gpio_interrupt_init();

    xTaskCreate(accel_task, "accel", ACCEL_TASK_STACK_WORDS, NULL,
                ACCEL_TASK_PRIORITY, &s_accel_task_handle);

    ESP_ERROR_CHECK(adxl375_interrupt_configure(s_adxl, 0, 0x00));
    ESP_ERROR_CHECK(adxl375_interrupt_configure(s_adxl, ADXL375_INT_WATERMARK | ADXL375_INT_SINGLE_SHOCK | ADXL375_INT_ACTIVITY, 0x00));
    ESP_ERROR_CHECK(adxl375_fifo_configure(s_adxl, ADXL375_FIFO_MODE_STREAM, ADXL375_FIFO_WATERMARK_SAMPLES, false));
}
