#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "xpt2046_softspi.h"

#ifndef CONFIG_CYD_TOUCH_ENABLED
#define CONFIG_CYD_TOUCH_ENABLED 0
#endif
#ifndef CONFIG_CYD_TOUCH_PIN_INT
#define CONFIG_CYD_TOUCH_PIN_INT -1
#endif

/*
 * Control byte: S=1, A2-A0=000, MODE=0 (12 bit), SER/DFR=0 (differential),
 * PD1=PD0=0 (power down between conversions, PENIRQ enabled).
 */
#define XPT2046_CMD_PENIRQ_ENABLE 0x80
/* Pen-detect settles in microseconds; this is only so the logged level is meaningful. */
#define XPT2046_PENIRQ_SETTLE_US 100

static const char *TAG = "xpt2046_softspi";
static bool s_initialized = false;

static inline void xpt2046_softspi_sclk_low(void)
{
    gpio_set_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_SCLK, 0);
}

static inline void xpt2046_softspi_sclk_high(void)
{
    gpio_set_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_SCLK, 1);
}

static inline void xpt2046_softspi_mosi_write(int level)
{
    gpio_set_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_MOSI, level);
}

static inline int xpt2046_softspi_miso_read(void)
{
    return gpio_get_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_MISO);
}

static inline void xpt2046_softspi_cs_low(void)
{
    gpio_set_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_CS, 0);
}

static inline void xpt2046_softspi_cs_high(void)
{
    gpio_set_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_CS, 1);
}

static void xpt2046_softspi_transfer(uint8_t *read_data, const uint8_t *write_data, size_t len)
{
    while (len-- > 0) {
        uint8_t r = 0;
        uint8_t mask = 0x80;
        uint8_t d = *write_data++;
        do {
            xpt2046_softspi_sclk_low();
            xpt2046_softspi_mosi_write((d & mask) != 0);
            xpt2046_softspi_sclk_high();
            if (xpt2046_softspi_miso_read()) {
                r |= mask;
            }
        } while ((mask >>= 1) != 0);

        *read_data++ = r;
    }

    xpt2046_softspi_sclk_low();
}

static int xpt2046_softspi_penirq_level(void)
{
    if (CONFIG_CYD_TOUCH_PIN_INT < 0) {
        return -1;
    }
    return gpio_get_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT);
}

/*
 * Put the controller into a known state with PENIRQ enabled.
 *
 * The XPT2046 is not reset with the ESP32. Flashing, the reset button and a
 * reset from the monitor restart only the ESP32, and the controller keeps the
 * power-down bits of the last command it received. If that command had PD0=1,
 * PENIRQ stays disabled (always high). get_raw() sends nothing while PENIRQ is
 * high, so the driver would never send the command that enables it again, and
 * the touch stays dead until the power is cut. Observed on a CYD 2026-10-12.
 *
 * Why this command: 0x80 is the byte get_raw() already ends every read with
 * (data[56]), so the controller receives it several times a second in normal
 * use; sending it once here creates no state that normal operation does not.
 * PD1=PD0=0 is also the controller's power-on state. Its conversion result
 * (A2-A0=000 in differential mode) is meaningless and is discarded. The two
 * trailing bytes clock the conversion through to the end of the 24-clock
 * frame, so the controller finishes it and enters power-down with the new PD
 * bits rather than having CS raised mid-conversion. CS is driven high before
 * this, so the command starts on a fresh CS falling edge even if the ESP32 was
 * reset in the middle of a frame.
 *
 * English supplement: this must stay unconditional. It cannot be gated on the
 * PENIRQ level, because a high PENIRQ is exactly the symptom it repairs.
 */
static void xpt2046_softspi_send_penirq_enable(void)
{
    uint8_t data[3] = { XPT2046_CMD_PENIRQ_ENABLE, 0x00, 0x00 };

    xpt2046_softspi_cs_low();
    xpt2046_softspi_transfer(data, data, sizeof(data));
    xpt2046_softspi_cs_high();
}

esp_err_t xpt2046_softspi_init(void)
{
#if CONFIG_CYD_TOUCH_ENABLED
    if (s_initialized) {
        return ESP_OK;
    }

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_CYD_TOUCH_PIN_SCLK) |
                        (1ULL << CONFIG_CYD_TOUCH_PIN_MOSI) |
                        (1ULL << CONFIG_CYD_TOUCH_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out_cfg), TAG, "touch output GPIO config failed");

    gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_CYD_TOUCH_PIN_MISO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_cfg), TAG, "touch MISO GPIO config failed");

    if (CONFIG_CYD_TOUCH_PIN_INT >= 0) {
        /*
         * get_raw() reads this line, so the driver configures it itself rather
         * than relying on a caller. GPIO36 on the CYD is input only and has no
         * internal pull; the panel side provides the pull-up.
         */
        gpio_config_t int_cfg = {
            .pin_bit_mask = 1ULL << CONFIG_CYD_TOUCH_PIN_INT,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "touch INT GPIO config failed");
    }

    xpt2046_softspi_cs_high();
    xpt2046_softspi_sclk_low();
    xpt2046_softspi_mosi_write(0);

    int penirq_before = xpt2046_softspi_penirq_level();
    xpt2046_softspi_send_penirq_enable();
    esp_rom_delay_us(XPT2046_PENIRQ_SETTLE_US);
    int penirq_after = xpt2046_softspi_penirq_level();

    s_initialized = true;
    /*
     * PENIRQ before/after tells the stuck case apart on a device: held at boot
     * after a warm reset, a stuck controller shows before=1 after=0.
     */
    ESP_LOGI(TAG,
             "XPT2046 software SPI configured: SCLK=%d MOSI=%d MISO=%d CS=%d INT=%d; "
             "PENIRQ enabled (INT level before=%d after=%d)",
             CONFIG_CYD_TOUCH_PIN_SCLK,
             CONFIG_CYD_TOUCH_PIN_MOSI,
             CONFIG_CYD_TOUCH_PIN_MISO,
             CONFIG_CYD_TOUCH_PIN_CS,
             CONFIG_CYD_TOUCH_PIN_INT,
             penirq_before,
             penirq_after);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t xpt2046_softspi_get_raw(int16_t *x, int16_t *y, bool *touched)
{
#if CONFIG_CYD_TOUCH_ENABLED
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "touch driver not initialized");

    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    if (touched != NULL) {
        *touched = false;
    }

    if (CONFIG_CYD_TOUCH_PIN_INT >= 0 && gpio_get_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT) != 0) {
        return ESP_OK;
    }

    uint8_t data[57];
    memset(data, 0, sizeof(data));
    data[0] = 0x91;
    data[2] = 0xB1;
    data[4] = 0xD1;
    data[6] = 0xC1;
    data[56] = 0x80;
    memcpy(&data[8], data, 8);
    memcpy(&data[16], data, 16);
    memcpy(&data[32], data, 24);

    xpt2046_softspi_cs_low();
    xpt2046_softspi_transfer(data, data, sizeof(data));
    xpt2046_softspi_cs_high();

    size_t ix = 0;
    size_t iy = 0;
    size_t iz = 0;
    uint16_t xt[7];
    uint16_t yt[7];
    uint16_t zt[7];
    for (size_t j = 0; j < 7; ++j) {
        uint8_t *d = &data[j * 8];
        int x_raw = (int)((d[5] << 8) | d[6]) >> 3;
        int y_raw = (int)((d[1] << 8) | d[2]) >> 3;
        int z_raw = 0x3200 + y_raw - x_raw +
                    ((((int)d[3] << 8) | d[4]) - (((int)d[7] << 8) | d[8])) / 2;
        if (x_raw > 128 && x_raw <= 3968) {
            xt[ix++] = (uint16_t)x_raw;
        }
        if (y_raw > 128 && y_raw <= 3968) {
            yt[iy++] = (uint16_t)y_raw;
        }
        if (z_raw > 0) {
            zt[iz++] = (uint16_t)z_raw;
        }
    }

    if (ix < 3 || iy < 3 || iz < 3) {
        return ESP_OK;
    }

    for (size_t i = 0; i + 1 < ix; ++i) {
        for (size_t j = i + 1; j < ix; ++j) {
            if (xt[j] < xt[i]) {
                uint16_t tmp = xt[i];
                xt[i] = xt[j];
                xt[j] = tmp;
            }
        }
    }
    for (size_t i = 0; i + 1 < iy; ++i) {
        for (size_t j = i + 1; j < iy; ++j) {
            if (yt[j] < yt[i]) {
                uint16_t tmp = yt[i];
                yt[i] = yt[j];
                yt[j] = tmp;
            }
        }
    }
    for (size_t i = 0; i + 1 < iz; ++i) {
        for (size_t j = i + 1; j < iz; ++j) {
            if (zt[j] < zt[i]) {
                uint16_t tmp = zt[i];
                zt[i] = zt[j];
                zt[j] = tmp;
            }
        }
    }

    if (x != NULL) {
        *x = (int16_t)xt[ix >> 1];
    }
    if (y != NULL) {
        *y = (int16_t)yt[iy >> 1];
    }
    if (touched != NULL) {
        *touched = (zt[iz >> 1] >> 8) != 0;
    }
    return ESP_OK;
#else
    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    if (touched != NULL) {
        *touched = false;
    }
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
