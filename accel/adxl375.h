#ifndef ADXL375_H
#define ADXL375_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --- Table 12: SPI limits --------------------------------------------------- */
/** Maximum SPI clock frequency (MHz) — Table 12, fSCLK. */
#define ADXL375_SPI_FCLK_MAX_MHZ 5

/* --- Register 0x2C BW_RATE — Table 6, Table 8, Register 0x2C description ----- */
/** Bit D4: low power mode (Register 0x2C). */
#define ADXL375_BW_RATE_LOW_POWER (1U << 4)
/** Mask for rate bits D3:D0. */
#define ADXL375_BW_RATE_ODR_MASK 0x0FU

    /**
     * Output data rate selection, rate bits D3:D0 — Table 6 (normal power).
     * Combine with `ADXL375_BW_RATE_LOW_POWER` where Table 8 applies.
     */
    typedef enum
    {
        ADXL375_ODR_0_10_HZ = 0x0U,
        ADXL375_ODR_0_20_HZ = 0x1U,
        ADXL375_ODR_0_39_HZ = 0x2U,
        ADXL375_ODR_0_78_HZ = 0x3U,
        ADXL375_ODR_1_56_HZ = 0x4U,
        ADXL375_ODR_3_13_HZ = 0x5U,
        ADXL375_ODR_6_25_HZ = 0x6U,
        ADXL375_ODR_12_5_HZ = 0x7U,
        ADXL375_ODR_25_HZ = 0x8U,
        ADXL375_ODR_50_HZ = 0x9U,
        ADXL375_ODR_100_HZ = 0xAU,
        ADXL375_ODR_200_HZ = 0xBU,
        ADXL375_ODR_400_HZ = 0xCU,
        ADXL375_ODR_800_HZ = 0xDU,
        ADXL375_ODR_1600_HZ = 0xEU,
        ADXL375_ODR_3200_HZ = 0xFU,
    } adxl375_odr_t;

    /** Return the output data rate in Hz for the given ODR setting (Table 6). */
    float adxl375_odr_to_hz(adxl375_odr_t odr);

/** Reset / default BW_RATE value (100 Hz ODR, normal power) — Table 15. */
#define ADXL375_BW_RATE_DEFAULT ((uint8_t)ADXL375_ODR_100_HZ)

/** Convenience: 100 Hz ODR, rate bits only (same as reset default). */
#define ADXL375_BW_RATE_ODR_100HZ ((uint8_t)ADXL375_ODR_100_HZ)

/* --- Register 0x2D POWER_CTL — Register 0x2D description, Table 16 ---------- */
#define ADXL375_POWER_CTL_WAKEUP_MASK 0x03U

    /** Wakeup rate in sleep mode — Table 16 (Bits D1:D0). */
    typedef enum
    {
        ADXL375_WAKEUP_8_HZ = 0x0U,
        ADXL375_WAKEUP_4_HZ = 0x1U,
        ADXL375_WAKEUP_2_HZ = 0x2U,
        ADXL375_WAKEUP_1_HZ = 0x3U,
    } adxl375_wakeup_t;

#define ADXL375_POWER_CTL_SLEEP (1U << 2)
#define ADXL375_POWER_CTL_MEASURE (1U << 3)
#define ADXL375_POWER_CTL_AUTO_SLEEP (1U << 4)
#define ADXL375_POWER_CTL_LINK (1U << 5)

/* --- Register 0x31 DATA_FORMAT — Register 0x31 description ------------------ */
#define ADXL375_DATA_FORMAT_SELF_TEST (1U << 6)
#define ADXL375_DATA_FORMAT_SPI_3WIRE (1U << 5)
#define ADXL375_DATA_FORMAT_INT_INVERT (1U << 4)
#define ADXL375_DATA_FORMAT_JUSTIFY_MSb (1U << 2)

/* --- Registers 0x2E / 0x2F / 0x30 interrupt map (same layout) -------------- */
#define ADXL375_INT_DATA_READY (1U << 7)
#define ADXL375_INT_SINGLE_SHOCK (1U << 6)
#define ADXL375_INT_DOUBLE_SHOCK (1U << 5)
#define ADXL375_INT_ACTIVITY (1U << 4)
#define ADXL375_INT_INACTIVITY (1U << 3)
#define ADXL375_INT_WATERMARK (1U << 1)
#define ADXL375_INT_OVERRUN (1U << 0)

/* --- Register 0x2A SHOCK_AXES — axis enable bits for shock detection -------- */
/** 1 LSB = 780 mg (THRESH_SHOCK, register 0x1D). */
#define ADXL375_SHOCK_THRESH_MG_PER_LSB 780U
/** 1 LSB = 625 µs (DUR, register 0x21). */
#define ADXL375_SHOCK_DUR_US_PER_LSB 625U
#define ADXL375_SHOCK_AXES_X (1U << 0)
#define ADXL375_SHOCK_AXES_Y (1U << 1)
#define ADXL375_SHOCK_AXES_Z (1U << 2)

/* --- Register 0x27 ACT_INACT_CTL — activity detection (upper nibble) -------- */
/** 1 LSB = 780 mg (THRESH_ACT, register 0x24). */
#define ADXL375_ACT_THRESH_MG_PER_LSB 780U
/** AC-coupled activity detection (compares against a reference, not zero). */
#define ADXL375_ACT_CTL_AC     (1U << 7)
#define ADXL375_ACT_CTL_AXES_Z (1U << 6)
#define ADXL375_ACT_CTL_AXES_Y (1U << 5)
#define ADXL375_ACT_CTL_AXES_X (1U << 4)

    /* --- Register 0x38 FIFO_CTL — Table 9, Table 17 ------------------------------ */
    typedef enum
    {
        ADXL375_FIFO_MODE_BYPASS = 0x00U,  /**< D7:D6 = 00 — Table 17. */
        ADXL375_FIFO_MODE_FIFO = 0x40U,    /**< D7:D6 = 01 */
        ADXL375_FIFO_MODE_STREAM = 0x80U,  /**< D7:D6 = 10 */
        ADXL375_FIFO_MODE_TRIGGER = 0xC0U, /**< D7:D6 = 11 */
    } adxl375_fifo_mode_t;

#define ADXL375_FIFO_CTL_TRIGGER_INT2 (1U << 5)

    typedef struct adxl375 *adxl375_handle_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} adxl375_sample_t;

    typedef struct
    {
        spi_host_device_t host;
        int mosi_io;
        int miso_io;
        int sclk_io;
        int cs_io;
        /** SPI clock (Hz). Keep ≤ `ADXL375_SPI_FCLK_MAX_MHZ` MHz (Table 12). */
        int clock_hz;
        /** If true, `adxl375_deinit` calls `spi_bus_free` when this init owned the bus (`spi_bus_initialize` succeeded). */
        bool free_bus_on_deinit;
    } adxl375_spi_config_t;

    esp_err_t adxl375_spi_init(const adxl375_spi_config_t *cfg, adxl375_handle_t *out_handle);
    void adxl375_deinit(adxl375_handle_t handle);

    esp_err_t adxl375_read_reg(adxl375_handle_t handle, uint8_t reg, uint8_t *value);
    esp_err_t adxl375_write_reg(adxl375_handle_t handle, uint8_t reg, uint8_t value);

    /** Verify `ADXL375_REG_DEVID` == `ADXL375_DEVID`. */
    esp_err_t adxl375_check_id(adxl375_handle_t handle);

    /**
     * Write `ADXL375_REG_BW_RATE` and `ADXL375_REG_POWER_CTL`.
     * `bw_rate`: ODR nibble (`ADXL375_ODR_*`) optionally OR `ADXL375_BW_RATE_LOW_POWER`.
     * `power_ctl`: OR of `ADXL375_POWER_CTL_*` / wakeup (`ADXL375_WAKEUP_*`).
     */
    esp_err_t adxl375_configure(adxl375_handle_t handle, uint8_t bw_rate, uint8_t power_ctl);

    /** Raw 16-bit two's complement axis samples (registers 0x32–0x37). */
    esp_err_t adxl375_read_xyz_mg(adxl375_handle_t handle, int16_t *x, int16_t *y, int16_t *z);

    esp_err_t adxl375_data_format(adxl375_handle_t handle, bool self_test, bool spi_3wire, bool int_invert, bool justify_msb);

    /**
     * Configure FIFO_CTL register.
     * `samples` is FIFO watermark/trigger level in range 0..31.
     */
    esp_err_t adxl375_fifo_configure(adxl375_handle_t handle, adxl375_fifo_mode_t mode, uint8_t samples, bool trigger_on_int2);

    /**
     * Configure interrupt enable and mapping.
     * `enable_mask` bits turn corresponding interrupts on in INT_ENABLE.
     * `map_to_int2_mask` bits route selected interrupts to INT2, remaining to INT1.
     */
    esp_err_t adxl375_interrupt_configure(adxl375_handle_t handle, uint8_t enable_mask, uint8_t map_to_int2_mask);

    /**
     * Configure shock detection registers.
     * `thresh`: THRESH_SHOCK value (1 LSB = `ADXL375_SHOCK_THRESH_MG_PER_LSB` mg).
     * `dur`: DUR value (1 LSB = `ADXL375_SHOCK_DUR_US_PER_LSB` µs).
     * `axes_mask`: OR of `ADXL375_SHOCK_AXES_X/Y/Z` to select participating axes.
     */
    esp_err_t adxl375_shock_configure(adxl375_handle_t handle, uint8_t thresh, uint8_t dur, uint8_t axes_mask);

    /**
     * Configure activity detection registers.
     * `thresh`: THRESH_ACT value (1 LSB = `ADXL375_ACT_THRESH_MG_PER_LSB` mg).
     * `ctl_mask`: OR of `ADXL375_ACT_CTL_AC` and `ADXL375_ACT_CTL_AXES_X/Y/Z`.
     *             Only the upper nibble (activity bits) of ACT_INACT_CTL is modified.
     */
    esp_err_t adxl375_act_configure(adxl375_handle_t handle, uint8_t thresh, uint8_t ctl_mask);

    /** Read and return INT_SOURCE register value. */
    esp_err_t adxl375_read_int_source(adxl375_handle_t handle, uint8_t *int_source);

    /** Return number of unread FIFO samples (0..32). */
    esp_err_t adxl375_fifo_get_samples_count(adxl375_handle_t handle, uint8_t *samples);

    /**
     * Read up to `max_samples` samples from FIFO into `out_samples`.
     * `read_samples` returns actual number read.
     */
    esp_err_t adxl375_read_fifo_samples_mg(adxl375_handle_t handle, adxl375_sample_t *out_samples, size_t max_samples, size_t *read_samples);
#ifdef __cplusplus
}
#endif

#endif
