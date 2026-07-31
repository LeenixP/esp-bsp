/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_cst820.h"
#include "unity.h"
#include "unity_test_runner.h"

/* Hardcoded wiring of the Candis-S31 test board this test app runs on
 * (same convention as the other lcd_touch test apps). Adjust these defines
 * when running on a different board. */
#define TEST_TOUCH_I2C_PORT       (0)
#define TEST_TOUCH_I2C_SDA        (GPIO_NUM_8)
#define TEST_TOUCH_I2C_SCL        (GPIO_NUM_18)
#define TEST_TOUCH_GPIO_INT       (GPIO_NUM_3)
#define TEST_TOUCH_GPIO_RST       (GPIO_NUM_7)
/* Only needs to cover the controller's native coordinate range; not tied to
 * the display panel resolution. */
#define TEST_TOUCH_H_RES          (460)
#define TEST_TOUCH_V_RES          (460)

TEST_CASE("CST820 initializes over I2C", "[cst820][i2c]")
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = TEST_TOUCH_I2C_PORT,
        .sda_io_num = TEST_TOUCH_I2C_SDA,
        .scl_io_num = TEST_TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    TEST_ESP_OK(i2c_new_master_bus(&bus_config, &i2c_bus));

    esp_lcd_panel_io_handle_t touch_io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    TEST_ESP_OK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &touch_io));

    const esp_lcd_touch_config_t touch_config = {
        .x_max = TEST_TOUCH_H_RES,
        .y_max = TEST_TOUCH_V_RES,
        .rst_gpio_num = TEST_TOUCH_GPIO_RST,
        .int_gpio_num = TEST_TOUCH_GPIO_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
    };

    esp_lcd_touch_handle_t touch = NULL;
    TEST_ESP_OK(esp_lcd_touch_new_i2c_cst820(touch_io, &touch_config, &touch));

    TEST_ESP_OK(esp_lcd_touch_del(touch));
    TEST_ESP_OK(esp_lcd_panel_io_del(touch_io));
    TEST_ESP_OK(i2c_del_master_bus(i2c_bus));
}

void app_main(void)
{
    printf("CST820 test application\n");
    unity_run_menu();
}
