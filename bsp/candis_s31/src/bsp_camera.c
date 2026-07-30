/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_video_init.h"

#include "bsp/candis_s31.h"

static const char *TAG = "candis_camera";
static esp_cam_sensor_xclk_handle_t s_xclk;
static bool s_started;

esp_err_t bsp_camera_start(const bsp_camera_cfg_t *cfg)
{
    (void)cfg;
    if (s_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "main I2C init failed");
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_CAMERA, true),
                        TAG, "camera power-up failed");

    const esp_cam_sensor_xclk_config_t xclk_config = {
        .ledc_cfg = {
            .timer = LEDC_TIMER_1,
            .clk_cfg = LEDC_AUTO_CLK,
            .channel = CONFIG_BSP_CAMERA_XCLK_LEDC_CH,
            .xclk_freq_hz = BSP_CAMERA_XCLK_CLOCK_MHZ * 1000000,
            .xclk_pin = BSP_CAMERA_XCLK,
        },
    };
    esp_err_t error = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_LEDC,
                      &s_xclk);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_cam_sensor_xclk_start(s_xclk, &xclk_config);
    if (error != ESP_OK) {
        goto fail;
    }

    const esp_video_init_dvp_config_t dvp_config = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = bsp_i2c_get_handle(),
            .freq = 100000,
        },
        .reset_pin = BSP_CAMERA_RST,
        .pwdn_pin = BSP_CAMERA_PWDN,
        .dvp_pin = {
            .data_width = 8,
            .data_io = {
                BSP_CAMERA_D0, BSP_CAMERA_D1, BSP_CAMERA_D2, BSP_CAMERA_D3,
                BSP_CAMERA_D4, BSP_CAMERA_D5, BSP_CAMERA_D6, BSP_CAMERA_D7,
            },
            .vsync_io = BSP_CAMERA_VSYNC,
            .de_io = BSP_CAMERA_HSYNC,
            .pclk_io = BSP_CAMERA_PCLK,
            .xclk_io = BSP_CAMERA_XCLK,
        },
        .xclk_freq = BSP_CAMERA_XCLK_CLOCK_MHZ * 1000000,
    };
    const esp_video_init_config_t video_config = {
        .dvp = &dvp_config,
    };
    error = esp_video_init(&video_config);
    if (error == ESP_OK) {
        s_started = true;
        return ESP_OK;
    }

fail:
    if (s_xclk != NULL) {
        esp_cam_sensor_xclk_stop(s_xclk);
        esp_cam_sensor_xclk_free(s_xclk);
        s_xclk = NULL;
    }
    bsp_peripheral_power_set(BSP_PERIPHERAL_CAMERA, false);
    return error;
}

esp_err_t bsp_camera_stop(void)
{
    if (!s_started) {
        return bsp_peripheral_power_set(BSP_PERIPHERAL_CAMERA, false);
    }
    esp_err_t result = esp_video_deinit();
    if (s_xclk != NULL) {
        const esp_err_t stop_error = esp_cam_sensor_xclk_stop(s_xclk);
        if (result == ESP_OK && stop_error != ESP_OK) {
            result = stop_error;
        }
        const esp_err_t free_error = esp_cam_sensor_xclk_free(s_xclk);
        if (result == ESP_OK && free_error != ESP_OK) {
            result = free_error;
        }
        s_xclk = NULL;
    }
    s_started = false;
    const esp_err_t power_error =
        bsp_peripheral_power_set(BSP_PERIPHERAL_CAMERA, false);
    return result != ESP_OK ? result : power_error;
}
