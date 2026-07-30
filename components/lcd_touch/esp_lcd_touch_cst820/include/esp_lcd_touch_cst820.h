/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LCD touch: CST820
 */

#pragma once

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new CST820 touch driver
 *
 * @note The I2C bus and panel IO must be initialized before calling this function.
 *
 * @param io LCD panel IO handle created by `esp_lcd_new_panel_io_i2c()`
 * @param config Touch controller configuration
 * @param tp Returned touch controller handle
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if an argument is invalid
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - Other error codes propagated from GPIO or panel IO operations
 */
esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_touch_config_t *config,
                                       esp_lcd_touch_handle_t *tp);

/** Default 7-bit I2C address of the CST820 controller. */
#define ESP_LCD_TOUCH_IO_I2C_CST820_ADDRESS    (0x15)

/**
 * @brief Default I2C panel IO configuration for CST820
 */
#define ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG()              \
    {                                                     \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST820_ADDRESS,  \
        .scl_speed_hz = 400000,                           \
        .control_phase_bytes = 1,                         \
        .dc_bit_offset = 0,                               \
        .lcd_cmd_bits = 8,                                \
        .lcd_param_bits = 0,                              \
        .flags =                                          \
        {                                                 \
            .disable_control_phase = 1,                   \
        }                                                 \
    }

#ifdef __cplusplus
}
#endif
