/*
 * Copyright 2026, KONTRBND
 */
#define DT_DRV_COMPAT novatek_nv3030b

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "display_nv3030b.h"

// * Configuration and runtime data

struct nv3030b_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec dc;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec bl;
    uint16_t width;
    uint16_t height;
    uint16_t panel_offset;
    uint16_t x_padding;
    uint16_t y_padding;
};

struct nv3030b_data {
    bool initialized;
};

// * Declarations

static int nv3030b_spi_write(const struct nv3030b_config *cfg,
                             const uint8_t *buf, size_t len);
static int nv3030b_write_cmd(const struct nv3030b_config *cfg, uint8_t cmd);
static int nv3030b_write_data(const struct nv3030b_config *cfg,
                              const uint8_t *data, size_t len);
static int nv3030b_write_cmd_data(const struct nv3030b_config *cfg,
                                  uint8_t cmd,
                                  const uint8_t *data, size_t len);

static void nv3030b_hw_reset(const struct nv3030b_config *cfg);
static void nv3030b_backlight(const struct nv3030b_config *cfg, int on);
static int nv3030b_set_window(const struct nv3030b_config *cfg,
                              uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1);

static int nv3030b_clear(const struct nv3030b_config *cfg, uint16_t color);
static int nv3030b_fill_rect(const struct nv3030b_config *cfg,
                        uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1,
                        uint16_t color);
static int nv3030b_put_buffer(const struct nv3030b_config *cfg,
                             uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             const uint8_t *buf);

static int nv3030b_init(const struct device *dev);
static int nv3030b_gpio_init(const struct nv3030b_config *cfg);
static int nv3030b_init_sequence(const struct nv3030b_config *cfg);

static int nv3030b_blanking_on(const struct device *dev);
static int nv3030b_blanking_off(const struct device *dev);
static int nv3030b_set_pixel_format(const struct device *dev,
                                    enum display_pixel_format format);
static int nv3030b_set_orientation(const struct device *dev,
                                   enum display_orientation orientation);
static int nv3030b_write(const struct device *dev,
                         const uint16_t x,
                         const uint16_t y,
                         const struct display_buffer_descriptor *desc,
                         const void *buf);
static void nv3030b_get_capabilities(const struct device *dev,
                                    struct display_capabilities *caps);

// * Raw Communication

static int nv3030b_spi_write(const struct nv3030b_config *cfg,
                            const uint8_t *buf,
                            size_t len)
{
    struct spi_buf tx_buf = {
        .buf = (void *)buf,
        .len = len,
    };

    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    return spi_write_dt(&cfg->spi, &tx);
}

static int nv3030b_write_cmd(const struct nv3030b_config *cfg,
                             uint8_t cmd)
{
    gpio_pin_set_dt(&cfg->dc, 0);
    k_busy_wait(1);
    return nv3030b_spi_write(cfg, &cmd, 1);
}

static int nv3030b_write_data(const struct nv3030b_config *cfg,
                              const uint8_t *data,
                              size_t len)
{
    gpio_pin_set_dt(&cfg->dc, 1);
    k_busy_wait(1);
    return nv3030b_spi_write(cfg, data, len);
}

static int nv3030b_write_cmd_data(const struct nv3030b_config *cfg,
                                  uint8_t cmd,
                                  const uint8_t *data,
                                  size_t len)
{
    int ret;

    ret = nv3030b_write_cmd(cfg, cmd);
    if (ret < 0) return ret;

    return nv3030b_write_data(cfg, data, len);
}

// * Command APIs

static void nv3030b_hw_reset(const struct nv3030b_config *cfg)
{    
    gpio_pin_set_dt(&cfg->reset, 1);
    k_msleep(20);

    gpio_pin_set_dt(&cfg->reset, 0);
    k_msleep(120);

    k_msleep(50);
}

static void nv3030b_backlight(const struct nv3030b_config *cfg, int on)
{
    gpio_pin_set_dt(&cfg->bl, on);
}

static int nv3030b_set_window(const struct nv3030b_config *cfg,
                              uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    int ret;

    y0 += cfg->panel_offset;
    y1 += cfg->panel_offset;

    /* Column address */
    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;

    ret = nv3030b_write_cmd_data(cfg, NV3030B_CMD_CASET, data, 4);
    if (ret < 0) {
        return ret;
    }

    /* Row address */
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;

    ret = nv3030b_write_cmd_data(cfg, NV3030B_CMD_PASET, data, 4);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int nv3030b_clear(const struct nv3030b_config *cfg, uint16_t color) {
    return nv3030b_fill_rect(cfg, 0, 0, cfg->width-1, cfg->height-1, color);
}

static int nv3030b_fill_rect(const struct nv3030b_config *cfg,
                        uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1,
                        uint16_t color)
{
    int ret;

    ret = nv3030b_set_window(cfg, x0, y0, x1, y1);
    if (ret < 0) {
        return ret;
    }

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_RAMWR);
    if (ret < 0) {
        return ret;
    }

    gpio_pin_set_dt(&cfg->dc, 1);
    k_busy_wait(1);

    /* Stream pixels in chunks to avoid large stack usage */
    uint8_t buf[64];
    for (int i = 0; i < sizeof(buf); i += 2) {
        buf[i] = color >> 8;
        buf[i + 1] = color & 0xFF;
    }

    uint32_t total_pixels = (x1 - x0 + 1) * (y1 - y0 + 1);
    while (total_pixels > 0) {
        uint32_t pixels = MIN(total_pixels, sizeof(buf) / 2);
        ret = nv3030b_spi_write(cfg, buf, pixels * 2);
        if (ret < 0) {
            return ret;
        }
        total_pixels -= pixels;
    }

    return 0;
}

static int nv3030b_put_buffer(const struct nv3030b_config *cfg,
                              uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              const uint8_t *buf)
{
    int ret;

    ret = nv3030b_set_window(cfg, x, y, x + width - 1, y + height - 1);
    if (ret < 0) return ret;

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_RAMWR);
    if (ret < 0) {
        return ret;
    }

    gpio_pin_set_dt(&cfg->dc, 1);
    k_busy_wait(1);

    ret = nv3030b_spi_write(cfg, buf, width * height * 2);
    if (ret < 0) return ret;

    return 0;
}

// * Display API

static int nv3030b_init(const struct device *dev)
{
    const struct nv3030b_config *cfg = dev->config;
    struct nv3030b_data *data = dev->data;
    int ret;

    data->initialized = false;

    if (!spi_is_ready_dt(&cfg->spi)) {
        return -ENODEV;
    }

    if (!device_is_ready(cfg->dc.port)) {
        return -ENODEV;
    }

    if (!device_is_ready(cfg->reset.port)) {
        return -ENODEV;
    }

    if (!device_is_ready(cfg->bl.port)) {
        return -ENODEV;
    }

    ret = nv3030b_gpio_init(cfg);
    if (ret < 0) return ret;

    nv3030b_hw_reset(cfg);
    
    ret = nv3030b_init_sequence(cfg);
    if (ret < 0) return ret;

    nv3030b_backlight(cfg, 1);

    ret = nv3030b_clear(cfg, 0b0000000000000000);
    if (ret < 0) return ret;

    data->initialized = true;

    return 0;
}

static int nv3030b_gpio_init(const struct nv3030b_config *cfg)
{
    int ret;

    ret = gpio_pin_configure_dt(&cfg->dc, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&cfg->bl, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int nv3030b_init_sequence(const struct nv3030b_config *cfg)
{
    int ret;

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_SWRESET);
    if (ret < 0) return ret;
    k_msleep(150);

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_SLPOUT);
    if (ret < 0) return ret;
    k_msleep(120);
    
    uint8_t madctl = 0x00; // RGB
    ret = nv3030b_write_cmd_data(cfg, NV3030B_CMD_MADCTL, &madctl, 1);
    if (ret < 0) return ret;

    uint8_t colmod = 0x55;  // RGB565 - 16 bits/pixel
    ret = nv3030b_write_cmd_data(cfg, NV3030B_CMD_COLMOD, &colmod, 1);
    if (ret < 0) return ret;

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_INVON);  // So we see RGB instead of CMY
    if (ret < 0) return ret;

    ret = nv3030b_set_window(cfg, 0, 0, cfg->width-1, cfg->height-1);
    if (ret < 0) return ret;
    
    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_DISPON);
    if (ret < 0) return ret;
    k_msleep(20);

    return 0;
}

// * Zephyr API

static int nv3030b_blanking_on(const struct device *dev)
{
    const struct nv3030b_config *cfg = dev->config;
    int ret;

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_DISPOFF);
    if (ret < 0) return ret;

    nv3030b_backlight(cfg, 0);

    return 0;
}

static int nv3030b_blanking_off(const struct device *dev)
{
    const struct nv3030b_config *cfg = dev->config;
    int ret;

    ret = nv3030b_write_cmd(cfg, NV3030B_CMD_DISPON);
    if (ret < 0) return ret;

    nv3030b_backlight(cfg, 1);

    return 0;
}

static int nv3030b_set_pixel_format(const struct device *dev,
                                    enum display_pixel_format format)
{
    if (format != PIXEL_FORMAT_RGB_565) {
        return -ENOTSUP;
    }

    return 0;
}

// ? Possible via NV3030B_CMD_MADCTL?
static int nv3030b_set_orientation(const struct device *dev,
                                   enum display_orientation orientation)
{
    if (orientation != DISPLAY_ORIENTATION_NORMAL) {
        return -ENOTSUP;
    }

    return 0;
}

static int nv3030b_write(const struct device *dev,
                         const uint16_t x,
                         const uint16_t y,
                         const struct display_buffer_descriptor *desc,
                         const void *buf)
{
    const struct nv3030b_config *cfg = dev->config;
    int ret;

    if (desc->pitch < desc->width) {
        return -EINVAL;
    }

    ret = nv3030b_put_buffer(cfg, x, y, desc->width, desc->height, buf);
    if (ret < 0) return ret;
    
    return 0;
}

static void nv3030b_get_capabilities(const struct device *dev,
                                    struct display_capabilities *caps)
{
    const struct nv3030b_config *cfg = dev->config;

    memset(caps, 0, sizeof(*caps));
    caps->x_resolution = cfg->width;
    caps->y_resolution = cfg->height;
    caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
    caps->current_pixel_format = PIXEL_FORMAT_RGB_565;
    caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

// * DeviceTree plumbing


#define NV3030B_INST(inst)                                                  \
static const struct nv3030b_config nv3030b_config_##inst = {                \
    .spi = SPI_DT_SPEC_INST_GET(inst,                                       \
        SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB             \
        | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_HOLD_ON_CS,                   \
        0),                                                                 \
    .dc = GPIO_DT_SPEC_INST_GET(inst, dc_gpios),                            \
    .reset = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                      \
    .bl = GPIO_DT_SPEC_INST_GET(inst, bl_gpios),                            \
    .width = DT_INST_PROP(inst, width),                                     \
    .height = DT_INST_PROP(inst, height),                                   \
    .panel_offset = DT_INST_PROP(inst, panel_offset),                       \
};                                                                          \
                                                                            \
static struct nv3030b_data nv3030b_data_##inst;

#define NV3030B_DEVICE(inst)                                                \
DEVICE_DT_INST_DEFINE(                                                      \
    inst,                                                                   \
    nv3030b_init,                                                           \
    NULL,                                                                   \
    &nv3030b_data_##inst,                                                   \
    &nv3030b_config_##inst,                                                 \
    POST_KERNEL,                                                            \
    CONFIG_DISPLAY_INIT_PRIORITY,                                           \
    &nv3030b_api                                                            \
);

// * Driver registration

static const struct display_driver_api nv3030b_api = {
    .blanking_on = nv3030b_blanking_on,
    .blanking_off = nv3030b_blanking_off,
    .set_pixel_format = nv3030b_set_pixel_format,
    .set_orientation = nv3030b_set_orientation,
    .write = nv3030b_write,
    .get_capabilities = nv3030b_get_capabilities,
};

DT_INST_FOREACH_STATUS_OKAY(NV3030B_INST)
DT_INST_FOREACH_STATUS_OKAY(NV3030B_DEVICE)
