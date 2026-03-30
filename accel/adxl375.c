#include "adxl375.h"

#include <stdlib.h>
#include <string.h>

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "adxl375";

/** Typical scale factor, mg/LSB (Table 1, scale factor row). */
#define MG_PER_LSB 49

/* --- SPI command byte (Figures 25–27): R/W, MB, A5–A0 ------------------------ */
#define SPI_READ 0x80U
#define SPI_WRITE 0x00U
#define SPI_MULTI 0x40U
/** Address field mask (bits A5–A0). */
#define SPI_ADDR_MASK 0x3FU
#define FIFO_SAMPLES_MASK 0x3FU
#define FIFO_MAX_SAMPLES 32U

/**
 * Register addresses — Table 15, Register Map.
 * Reserved region 0x01–0x1C is omitted.
 */
typedef enum
{
    REG_DEVID = 0x00U,
    REG_THRESH_SHOCK = 0x1DU,
    REG_OFSX = 0x1EU,
    REG_OFSY = 0x1FU,
    REG_OFSZ = 0x20U,
    REG_DUR = 0x21U,
    REG_LATENT = 0x22U,
    REG_WINDOW = 0x23U,
    REG_THRESH_ACT = 0x24U,
    REG_THRESH_INACT = 0x25U,
    REG_TIME_INACT = 0x26U,
    REG_ACT_INACT_CTL = 0x27U,
    REG_SHOCK_AXES = 0x2AU,
    REG_ACT_SHOCK_STATUS = 0x2BU,
    REG_BW_RATE = 0x2CU,
    REG_POWER_CTL = 0x2DU,
    REG_INT_ENABLE = 0x2EU,
    REG_INT_MAP = 0x2FU,
    REG_INT_SOURCE = 0x30U,
    REG_DATA_FORMAT = 0x31U,
    REG_DATAX0 = 0x32U,
    REG_DATAX1 = 0x33U,
    REG_DATAY0 = 0x34U,
    REG_DATAY1 = 0x35U,
    REG_DATAZ0 = 0x36U,
    REG_DATAZ1 = 0x37U,
    REG_FIFO_CTL = 0x38U,
    REG_FIFO_STATUS = 0x39U,
} adxl375_reg_t;

#define REG_DEVID_VALUE 0xE5U

struct adxl375
{
    spi_device_handle_t spi;
    spi_host_device_t host;
    bool free_bus_on_deinit;
    bool host_inited_here;
};

static esp_err_t spi_tx_rx(adxl375_handle_t dev, const void *tx, void *rx, size_t len)
{
    spi_transaction_t t = {
        .length = (uint32_t)(len * 8U),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t adxl375_read_multi(adxl375_handle_t handle, uint8_t start_reg, uint8_t *data, size_t data_len)
{
    ESP_RETURN_ON_FALSE(handle && data && data_len > 0, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t tx[7] = {0};
    uint8_t rx[7] = {0};
    ESP_RETURN_ON_FALSE(data_len <= (sizeof(tx) - 1U), ESP_ERR_INVALID_SIZE, TAG, "data_len");

    tx[0] = (uint8_t)(SPI_READ | SPI_MULTI | (start_reg & SPI_ADDR_MASK));
    esp_err_t err = spi_tx_rx(handle, tx, rx, data_len + 1U);
    ESP_RETURN_ON_ERROR(err, TAG, "spi");
    memcpy(data, &rx[1], data_len);
    return ESP_OK;
}

esp_err_t adxl375_read_reg(adxl375_handle_t handle, uint8_t reg, uint8_t *value)
{
    ESP_RETURN_ON_FALSE(handle && value, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t tx[2] = {(uint8_t)(SPI_READ | (reg & 0x3FU)), 0};
    uint8_t rx[2];

    esp_err_t err = spi_tx_rx(handle, tx, rx, sizeof(tx));
    ESP_RETURN_ON_ERROR(err, TAG, "spi");

    *value = rx[1];
    return ESP_OK;
}

esp_err_t adxl375_write_reg(adxl375_handle_t handle, uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t tx[2] = {(uint8_t)(reg & 0x3FU), value};
    return spi_tx_rx(handle, tx, NULL, sizeof(tx));
}

esp_err_t adxl375_check_id(adxl375_handle_t handle)
{
    uint8_t id = 1;
    esp_err_t err = adxl375_read_reg(handle, REG_DEVID, &id);
    ESP_RETURN_ON_ERROR(err, TAG, "read devid");

    if (id != REG_DEVID_VALUE)
    {
        ESP_LOGE(TAG, "unexpected DEVID 0x%02x (expected 0x%02x)", id, REG_DEVID_VALUE);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t adxl375_configure(adxl375_handle_t handle, uint8_t bw_rate, uint8_t power_ctl)
{
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_BW_RATE, bw_rate), TAG, "bw_rate");
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_POWER_CTL, power_ctl), TAG, "power_ctl");
    return ESP_OK;
}

esp_err_t adxl375_data_format(adxl375_handle_t handle, bool self_test, bool spi_3wire, bool int_invert, bool justify_msb)
{
    uint8_t reg_value = 0;
    reg_value |= self_test ? ADXL375_DATA_FORMAT_SELF_TEST : 0;
    reg_value |= spi_3wire ? ADXL375_DATA_FORMAT_SPI_3WIRE : 0;
    reg_value |= int_invert ? ADXL375_DATA_FORMAT_INT_INVERT : 0;
    reg_value |= justify_msb ? ADXL375_DATA_FORMAT_JUSTIFY_MSb : 0;

    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_DATA_FORMAT, reg_value), TAG, "data_format");
    return ESP_OK;
}

esp_err_t adxl375_read_xyz(adxl375_handle_t handle, int16_t *x, int16_t *y, int16_t *z)
{
    ESP_RETURN_ON_FALSE(handle && x && y && z, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t data[6] = {0};
    esp_err_t err = adxl375_read_multi(handle, REG_DATAX0, data, sizeof(data));
    ESP_RETURN_ON_ERROR(err, TAG, "read xyz");

    *x = (int16_t)((uint16_t)data[1] << 8 | data[0]) * MG_PER_LSB;
    *y = (int16_t)((uint16_t)data[3] << 8 | data[2]) * MG_PER_LSB;
    *z = (int16_t)((uint16_t)data[5] << 8 | data[4]) * MG_PER_LSB;
    return ESP_OK;
}

esp_err_t adxl375_fifo_configure(adxl375_handle_t handle, adxl375_fifo_mode_t mode, uint8_t samples, bool trigger_on_int2)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    ESP_RETURN_ON_FALSE(samples <= 31U, ESP_ERR_INVALID_ARG, TAG, "samples");

    uint8_t fifo_ctl = (uint8_t)mode;
    fifo_ctl |= trigger_on_int2 ? ADXL375_FIFO_CTL_TRIGGER_INT2 : 0U;
    fifo_ctl |= (samples & 0x1FU);

    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_FIFO_CTL, fifo_ctl), TAG, "fifo_ctl");
    return ESP_OK;
}

esp_err_t adxl375_interrupt_configure(adxl375_handle_t handle, uint8_t enable_mask, uint8_t map_to_int2_mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_INT_MAP, map_to_int2_mask), TAG, "int_map");
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_INT_ENABLE, enable_mask), TAG, "int_enable");
    return ESP_OK;
}

esp_err_t adxl375_shock_configure(adxl375_handle_t handle, uint8_t thresh, uint8_t dur, uint8_t axes_mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_THRESH_SHOCK, thresh), TAG, "thresh_shock");
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_DUR, dur), TAG, "dur");
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_SHOCK_AXES, axes_mask & 0x07U), TAG, "shock_axes");
    return ESP_OK;
}

esp_err_t adxl375_act_configure(adxl375_handle_t handle, uint8_t thresh, uint8_t ctl_mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_THRESH_ACT, thresh), TAG, "thresh_act");

    uint8_t act_inact_ctl = 0;
    ESP_RETURN_ON_ERROR(adxl375_read_reg(handle, REG_ACT_INACT_CTL, &act_inact_ctl), TAG, "act_inact_ctl read");
    act_inact_ctl = (uint8_t)((act_inact_ctl & 0x0FU) | (ctl_mask & 0xF0U));
    ESP_RETURN_ON_ERROR(adxl375_write_reg(handle, REG_ACT_INACT_CTL, act_inact_ctl), TAG, "act_inact_ctl write");
    return ESP_OK;
}

esp_err_t adxl375_read_int_source(adxl375_handle_t handle, uint8_t *int_source)
{
    ESP_RETURN_ON_FALSE(handle && int_source, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    return adxl375_read_reg(handle, REG_INT_SOURCE, int_source);
}

esp_err_t adxl375_fifo_get_samples_count(adxl375_handle_t handle, uint8_t *samples)
{
    ESP_RETURN_ON_FALSE(handle && samples, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t fifo_status = 0;
    ESP_RETURN_ON_ERROR(adxl375_read_reg(handle, REG_FIFO_STATUS, &fifo_status), TAG, "fifo_status");
    *samples = (uint8_t)(fifo_status & FIFO_SAMPLES_MASK);
    return ESP_OK;
}

esp_err_t adxl375_read_fifo_samples(adxl375_handle_t handle, adxl375_sample_t *out_samples, size_t max_samples, size_t *read_samples)
{
    ESP_RETURN_ON_FALSE(handle && out_samples && read_samples, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    ESP_RETURN_ON_FALSE(max_samples > 0U, ESP_ERR_INVALID_ARG, TAG, "max_samples");

    uint8_t fifo_samples = 0;
    ESP_RETURN_ON_ERROR(adxl375_fifo_get_samples_count(handle, &fifo_samples), TAG, "fifo count");

    size_t to_read = fifo_samples;
    if (to_read > max_samples)
    {
        to_read = max_samples;
    }

    for (size_t i = 0; i < to_read; ++i)
    {
        uint8_t data[6] = {0};
        ESP_RETURN_ON_ERROR(adxl375_read_multi(handle, REG_DATAX0, data, sizeof(data)), TAG, "fifo read");
        out_samples[i].x = (int16_t)((uint16_t)data[1] << 8 | data[0]) * MG_PER_LSB;
        out_samples[i].y = (int16_t)((uint16_t)data[3] << 8 | data[2]) * MG_PER_LSB;
        out_samples[i].z = (int16_t)((uint16_t)data[5] << 8 | data[4]) * MG_PER_LSB;
    }

    *read_samples = to_read;
    return ESP_OK;
}

// esp_err_t adxl375_clear_fifo_samples(adxl375_handle_t handle, adxl375_sample_t *out_samples, size_t max_samples, size_t *read_samples)
// {
//     ESP_RETURN_ON_FALSE(handle && out_samples && read_samples, ESP_ERR_INVALID_ARG, TAG, "bad arg");
//     ESP_RETURN_ON_FALSE(max_samples > 0U, ESP_ERR_INVALID_ARG, TAG, "max_samples");

//     uint8_t fifo_samples = 0;
//     ESP_RETURN_ON_ERROR(adxl375_fifo_get_samples_count(handle, &fifo_samples), TAG, "fifo count");

//     size_t to_read = fifo_samples;
//     if (to_read > max_samples)
//     {
//         to_read = max_samples;
//     }

//     for (size_t i = 0; i < to_read; ++i)
//     {
//         uint8_t data[6] = {0};
//         ESP_RETURN_ON_ERROR(adxl375_read_multi(handle, REG_DATAX0, data, sizeof(data)), TAG, "fifo read");
//         out_samples[i].x = (int16_t)((uint16_t)data[1] << 8 | data[0]) * MG_PER_LSB;
//         out_samples[i].y = (int16_t)((uint16_t)data[3] << 8 | data[2]) * MG_PER_LSB;
//         out_samples[i].z = (int16_t)((uint16_t)data[5] << 8 | data[4]) * MG_PER_LSB;
//     }

//     *read_samples = to_read;
//     return ESP_OK;
// }

esp_err_t adxl375_spi_init(const adxl375_spi_config_t *cfg, adxl375_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    ESP_RETURN_ON_FALSE(cfg->clock_hz > 0, ESP_ERR_INVALID_ARG, TAG, "clock_hz");

    *out_handle = NULL;

    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->mosi_io,
        .miso_io_num = cfg->miso_io,
        .sclk_io_num = cfg->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    esp_err_t err = spi_bus_initialize(cfg->host, &buscfg, SPI_DMA_DISABLED);
    const bool host_inited_here = (err == ESP_OK);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_RETURN_ON_ERROR(err, TAG, "spi_bus_initialize");
    }

    spi_device_interface_config_t devcfg = {
        .mode = 3,
        .clock_speed_hz = cfg->clock_hz,
        .spics_io_num = cfg->cs_io,
        .queue_size = 3,
    };

    spi_device_handle_t spi = NULL;
    err = spi_bus_add_device(cfg->host, &devcfg, &spi);
    if (err != ESP_OK)
    {
        if (host_inited_here)
        {
            spi_bus_free(cfg->host);
        }
        ESP_RETURN_ON_ERROR(err, TAG, "spi_bus_add_device");
    }

    adxl375_handle_t dev = calloc(1, sizeof(*dev));
    if (!dev)
    {
        spi_bus_remove_device(spi);
        if (host_inited_here)
        {
            spi_bus_free(cfg->host);
        }
        return ESP_ERR_NO_MEM;
    }

    dev->spi = spi;
    dev->host = cfg->host;
    dev->free_bus_on_deinit = cfg->free_bus_on_deinit;
    dev->host_inited_here = host_inited_here;

    *out_handle = dev;
    return ESP_OK;
}

void adxl375_deinit(adxl375_handle_t handle)
{
    if (!handle)
    {
        return;
    }

    if (handle->spi)
    {
        spi_bus_remove_device(handle->spi);
        handle->spi = NULL;
    }

    if (handle->free_bus_on_deinit && handle->host_inited_here)
    {
        spi_bus_free(handle->host);
    }

    free(handle);
}
