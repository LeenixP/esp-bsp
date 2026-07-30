/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "led_indicator_strips.h"

#include "bsp/candis_s31.h"

extern blink_step_t const *bsp_led_blink_defaults_lists[];

static const char *TAG = "candis_led";

/* The WS2812B is powered from the TG28 DC1SW output, which the schematic
 * straps in fixed mode: the rail follows the main 3.3 V domain and needs
 * no software control. */
static const led_strip_config_t s_strip_config = {
    .strip_gpio_num = BSP_LED_RGB_IO,
    .max_leds = 1,
    .led_model = LED_MODEL_WS2812,
    .flags.invert_out = false,
};

#if CONFIG_BSP_LED_RGB_BACKEND_RMT
static const led_strip_rmt_config_t s_rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .flags.with_dma = false,
};
#elif CONFIG_BSP_LED_RGB_BACKEND_SPI
static const led_strip_spi_config_t s_spi_config = {
    .spi_bus = SPI3_HOST,
    .flags.with_dma = true,
};
#else
#error "Select an RGB LED backend"
#endif

static led_indicator_strips_config_t s_rgb_config = {
    .led_strip_cfg = s_strip_config,
#if CONFIG_BSP_LED_RGB_BACKEND_RMT
    .led_strip_driver = LED_STRIP_RMT,
    .led_strip_rmt_cfg = s_rmt_config,
#else
    .led_strip_driver = LED_STRIP_SPI,
    .led_strip_spi_cfg = s_spi_config,
#endif
};

static const led_indicator_config_t s_indicator_config = {
    .blink_lists = bsp_led_blink_defaults_lists,
    .blink_list_num = BSP_LED_MAX,
};

esp_err_t bsp_led_indicator_create(led_indicator_handle_t led_array[],
                                   int *led_cnt, int led_array_size)
{
    ESP_RETURN_ON_FALSE(led_array != NULL && led_array_size >= BSP_LED_NUM,
                        ESP_ERR_INVALID_ARG, TAG, "LED array is too small");
    if (led_cnt != NULL) {
        *led_cnt = 0;
    }
    for (int index = 0; index < BSP_LED_NUM; ++index) {
        ESP_RETURN_ON_ERROR(led_indicator_new_strips_device(&s_indicator_config,
                            &s_rgb_config,
                            &led_array[index]),
                            TAG, "RGB LED creation failed");
        if (led_cnt != NULL) {
            ++(*led_cnt);
        }
    }
    return ESP_OK;
}

esp_err_t bsp_led_set(led_indicator_handle_t handle, bool on)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "LED handle is NULL");
    return led_indicator_start(handle, on ? BSP_LED_ON : BSP_LED_OFF);
}
