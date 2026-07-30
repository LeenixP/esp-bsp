/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/candis_s31.h"

static const char *TAG = "candis_display";
static bsp_lcd_handles_t s_display;
static bool s_spi_initialized;
static esp_lcd_touch_handle_t s_touch;
static esp_lcd_panel_io_handle_t s_touch_io;

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *s_lvgl_display;
static lv_indev_t *s_lvgl_touch;
static bool s_lvgl_initialized;
#endif

/* AM200Q460460LK supplier initialization sequence, converted to esp_lcd. */
static const co5300_lcd_init_cmd_t s_panel_init[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    /* The active 460-pixel window starts at column 10. */
    {0x2A, (uint8_t[]){0x00, 0x0A, 0x01, 0xD5}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xCB}, 4, 0},
    {0x11, NULL, 0, 60},
    {0x29, NULL, 0, 0},
};

esp_err_t bsp_display_brightness_init(void)
{
    return s_display.panel != NULL ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t bsp_display_brightness_deinit(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    ESP_RETURN_ON_FALSE(s_display.panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "display is not initialized");
    ESP_RETURN_ON_FALSE(brightness_percent >= 0 && brightness_percent <= 100,
                        ESP_ERR_INVALID_ARG, TAG, "brightness must be 0..100");
    return esp_lcd_panel_co5300_set_brightness(s_display.panel,
            (uint8_t)brightness_percent);
}

esp_err_t bsp_display_backlight_off(void)
{
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(0), TAG,
                        "brightness update failed");
    return esp_lcd_panel_disp_on_off(s_display.panel, false);
}

esp_err_t bsp_display_backlight_on(void)
{
    ESP_RETURN_ON_FALSE(s_display.panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "display is not initialized");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_display.panel, true), TAG,
                        "display enable failed");
    return bsp_display_brightness_set(100);
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    ESP_RETURN_ON_FALSE(ret_panel != NULL && ret_io != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "display return handle is NULL");
    bsp_lcd_handles_t handles = {0};
    ESP_RETURN_ON_ERROR(bsp_display_new_with_handles(config, &handles), TAG,
                        "display creation failed");
    *ret_panel = handles.panel;
    *ret_io = handles.io;
    return ESP_OK;
}

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config,
                                       bsp_lcd_handles_t *ret_handles)
{
    ESP_RETURN_ON_FALSE(ret_handles != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "display return handles are NULL");
    ESP_RETURN_ON_FALSE(s_display.panel == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "display is already initialized");

    const int max_transfer_size = config != NULL && config->max_transfer_sz > 0 ?
                                  config->max_transfer_sz :
                                  BSP_LCD_H_RES * 80 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_DISPLAY, true),
                        TAG, "display power sequence failed");

    const spi_bus_config_t bus_config =
        CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_QSPI_CLK,
                                     BSP_LCD_QSPI_DATA0,
                                     BSP_LCD_QSPI_DATA1,
                                     BSP_LCD_QSPI_DATA2,
                                     BSP_LCD_QSPI_DATA3,
                                     max_transfer_size);
    esp_err_t error = spi_bus_initialize(BSP_LCD_SPI_NUM, &bus_config,
                                         SPI_DMA_CH_AUTO);
    if (error != ESP_OK) {
        goto fail;
    }
    s_spi_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_config.pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    error = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM,
                                     &io_config, &s_display.io);
    if (error != ESP_OK) {
        goto fail;
    }

    const co5300_vendor_config_t vendor_config = {
        .init_cmds = s_panel_init,
        .init_cmds_size = sizeof(s_panel_init) / sizeof(s_panel_init[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = (void *) &vendor_config,
    };
    error = esp_lcd_new_panel_co5300(s_display.io, &panel_config,
                                     &s_display.panel);
    if (error != ESP_OK) {
        goto fail;
    }

    error = esp_lcd_panel_reset(s_display.panel);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_lcd_panel_init(s_display.panel);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_lcd_panel_set_gap(s_display.panel, BSP_LCD_X_GAP, BSP_LCD_Y_GAP);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_lcd_panel_disp_on_off(s_display.panel, false);
    if (error != ESP_OK) {
        goto fail;
    }

    *ret_handles = s_display;
    ESP_LOGI(TAG, "CO5300 initialized at %dx%d, QSPI %d MHz",
             BSP_LCD_H_RES, BSP_LCD_V_RES, CONFIG_BSP_LCD_PIXEL_CLOCK_MHZ);
    return ESP_OK;

fail:
    bsp_display_delete();
    return error;
}

void bsp_display_delete(void)
{
    if (s_display.panel != NULL) {
        esp_lcd_panel_disp_on_off(s_display.panel, false);
        esp_lcd_panel_del(s_display.panel);
        s_display.panel = NULL;
    }
    if (s_display.io != NULL) {
        esp_lcd_panel_io_del(s_display.io);
        s_display.io = NULL;
    }
    if (s_spi_initialized) {
        spi_bus_free(BSP_LCD_SPI_NUM);
        s_spi_initialized = false;
    }
    bsp_peripheral_power_set(BSP_PERIPHERAL_DISPLAY, false);
}

esp_err_t bsp_touch_new(const bsp_touch_config_t *config,
                        esp_lcd_touch_handle_t *ret_touch)
{
    ESP_RETURN_ON_FALSE(ret_touch != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "touch return handle is NULL");
    if (s_touch != NULL) {
        *ret_touch = s_touch;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_TOUCH, true),
                        TAG, "touch power sequence failed");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "main I2C init failed");

    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    io_config.scl_speed_hz = 400000;
    esp_err_t error = esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(),
                      &io_config, &s_touch_io);
    if (error != ESP_OK) {
        bsp_peripheral_power_set(BSP_PERIPHERAL_TOUCH, false);
        return error;
    }

    const bsp_touch_config_t default_config = {0};
    const bsp_touch_config_t *orientation = config != NULL ? config : &default_config;
    const esp_lcd_touch_config_t touch_config = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_RST,
        .int_gpio_num = BSP_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = orientation->swap_xy,
            .mirror_x = orientation->mirror_x,
            .mirror_y = orientation->mirror_y,
        },
    };
    error = esp_lcd_touch_new_i2c_cst820(s_touch_io, &touch_config, &s_touch);
    if (error != ESP_OK) {
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
        bsp_peripheral_power_set(BSP_PERIPHERAL_TOUCH, false);
        return error;
    }
    *ret_touch = s_touch;
    return ESP_OK;
}

esp_err_t bsp_touch_delete(void)
{
    esp_err_t result = ESP_OK;
    if (s_touch != NULL) {
        result = esp_lcd_touch_del(s_touch);
        if (result != ESP_OK) {
            return result;
        }
        s_touch = NULL;
    }
    if (s_touch_io != NULL) {
        result = esp_lcd_panel_io_del(s_touch_io);
        if (result != ESP_OK) {
            return result;
        }
        s_touch_io = NULL;
    }
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_TOUCH, false),
                        TAG, "touch power-down failed");
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return s_touch;
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *display_lvgl_init(const bsp_display_cfg_t *config)
{
    const bsp_display_config_t panel_config = {
        .max_transfer_sz = (int)(BSP_LCD_H_RES *CONFIG_BSP_LCD_DRAW_BUF_HEIGHT *
                                 sizeof(uint16_t)),
    };
    if (bsp_display_new(&panel_config, &s_display.panel, &s_display.io) != ESP_OK) {
        return NULL;
    }
    if (bsp_display_backlight_on() != ESP_OK) {
        return NULL;
    }

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = s_display.io,
        .panel_handle = s_display.panel,
        .buffer_size = config->buffer_size,
        .double_buffer = config->double_buffer,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = config->flags.buff_dma,
            .buff_spiram = config->flags.buff_spiram,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = BSP_LCD_BIGENDIAN,
#endif
            .sw_rotate = config->flags.sw_rotate,
        },
    };
    return lvgl_port_add_disp(&display_config);
}

lv_display_t *bsp_display_start(void)
{
    const bsp_display_cfg_t config = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = true,
#else
        .double_buffer = false,
#endif
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        },
    };
    return bsp_display_start_with_config(&config);
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *config)
{
    if (config == NULL || s_lvgl_initialized) {
        return NULL;
    }
    if (lvgl_port_init(&config->lvgl_port_cfg) != ESP_OK) {
        return NULL;
    }
    s_lvgl_initialized = true;
    s_lvgl_display = display_lvgl_init(config);
    if (s_lvgl_display == NULL) {
        bsp_display_stop();
        return NULL;
    }
    if (bsp_touch_new(NULL, &s_touch) != ESP_OK) {
        bsp_display_stop();
        return NULL;
    }
    const lvgl_port_touch_cfg_t touch_config = {
        .disp = s_lvgl_display,
        .handle = s_touch,
    };
    s_lvgl_touch = lvgl_port_add_touch(&touch_config);
    if (s_lvgl_touch == NULL) {
        bsp_display_stop();
        return NULL;
    }
    return s_lvgl_display;
}

esp_err_t bsp_display_stop(void)
{
    esp_err_t result = ESP_OK;
    if (s_lvgl_touch != NULL) {
        result = lvgl_port_remove_touch(s_lvgl_touch);
        if (result != ESP_OK) {
            return result;
        }
        s_lvgl_touch = NULL;
    }
    if (s_lvgl_display != NULL) {
        result = lvgl_port_remove_disp(s_lvgl_display);
        if (result != ESP_OK) {
            return result;
        }
        s_lvgl_display = NULL;
    }
    if (s_lvgl_initialized) {
        result = lvgl_port_deinit();
        if (result != ESP_OK) {
            return result;
        }
        s_lvgl_initialized = false;
    }
    ESP_RETURN_ON_ERROR(bsp_touch_delete(), TAG, "touch delete failed");
    bsp_display_delete();
    return ESP_OK;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return s_lvgl_touch;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

void bsp_display_rotate(lv_display_t *display, lv_display_rotation_t rotation)
{
    lv_display_set_rotation(display, rotation);
}

esp_err_t bsp_display_enter_sleep(void)
{
    ESP_RETURN_ON_FALSE(s_display.panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "display is not initialized");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_off(), TAG, "display off failed");
    if (s_touch != NULL) {
        const esp_err_t touch_error = esp_lcd_touch_enter_sleep(s_touch);
        if (touch_error != ESP_OK && touch_error != ESP_ERR_NOT_SUPPORTED) {
            return touch_error;
        }
    }
    return esp_lcd_panel_io_tx_param(s_display.io, LCD_CMD_SLPIN, NULL, 0);
}

esp_err_t bsp_display_exit_sleep(void)
{
    ESP_RETURN_ON_FALSE(s_display.panel != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "display is not initialized");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_display.io, LCD_CMD_SLPOUT,
                        NULL, 0),
                        TAG, "display sleep-out failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    if (s_touch != NULL) {
        const esp_err_t touch_error = esp_lcd_touch_exit_sleep(s_touch);
        if (touch_error != ESP_OK && touch_error != ESP_ERR_NOT_SUPPORTED) {
            return touch_error;
        }
    }
    return bsp_display_backlight_on();
}
#endif
