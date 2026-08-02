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

/* The OV5640 register tables in esp_cam_sensor (sensors/ov5640) are all
 * calculated for a 24 MHz XCLK input ("24M input" in every format option);
 * any other frequency shifts frame rate and exposure timing.
 * BSP_CAMERA_XCLK_CLOCK_MHZ must stay 24. */
_Static_assert(BSP_CAMERA_XCLK_CLOCK_MHZ == 24,
               "OV5640 sensor register tables assume a 24 MHz XCLK");

esp_err_t bsp_camera_start(const bsp_camera_cfg_t *cfg)
{
    (void)cfg;
    if (s_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "main I2C init failed");
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_CAMERA, true),
                        TAG, "camera power-up failed");

    /* NOTE: XCLK ends up driven twice. This LEDC channel outputs on
     * BSP_CAMERA_XCLK first, then esp_video_init_with_flags() routes the DVP
     * controller's own camera clock to the same pin (esp_video_init.c calls
     * esp_cam_ctlr_dvp_output_clock when xclk_io >= 0 && xclk_freq > 0),
     * which silently wins the GPIO matrix output selection and leaves the
     * LEDC channel redundant. Same flow as the official esp32_s31_korvo_1
     * BSP; kept because it is harmless while both clocks are 24 MHz.
     * EVT: probe the XCLK pin and confirm the effective clock is 24 MHz. */
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
    /* TODO(af): the production OV5640 module has autofocus. The VCM is
     * suspected to be a DW9714 at SCCB address 0x0C (DW9714_SCCB_ADDR in
     * esp_cam_sensor/motors/dw9714), still awaiting written confirmation from
     * the module vendor. The BSP intentionally leaves
     * esp_video_init_config_t.cam_motor unset and EVT1 units run fixed focus
     * (AF unused).
     *
     * Enable steps once the VCM is confirmed:
     * 1. Get written confirmation of the VCM part number and SCCB address
     *    from the module vendor.
     * 2. Enable in sdkconfig: CONFIG_ESP_VIDEO_ENABLE_CAMERA_MOTOR_CONTROLLER
     *    and CONFIG_CAM_MOTOR_DW9714 (esp_cam_sensor Kconfig; its default
     *    auto-detect loads the driver during startup).
     * 3. Uncomment the cam_motor block below and OR
     *    ESP_VIDEO_INIT_FLAGS_MOTOR into the flags passed to
     *    esp_video_init_with_flags().
     *
     * The motor shares the sensor's SCCB bus (esp_video_init_cam_motor_config_t
     * in esp_video_init.h; esp_video_init maps it onto esp_cam_motor_config_t
     * in esp_cam_motor_types.h and calls dw9714_detect()). The production
     * module's reset/pwdn/signal pin usage is not confirmed yet - the VCM may
     * share the sensor's RST (GPIO39) / PWDN (GPIO40), or have none. */
    /* const esp_video_init_cam_motor_config_t cam_motor_config = {
     *     .sccb_config = {
     *         .init_sccb = false,
     *         .i2c_handle = bsp_i2c_get_handle(),
     *         .freq = 100000,
     *     },
     *     .reset_pin = GPIO_NUM_NC,   // -1 if the module has no VCM reset pin
     *     .pwdn_pin = GPIO_NUM_NC,    // -1 if the module has no VCM pwdn pin
     *     .signal_pin = GPIO_NUM_NC,  // -1 if the module has no VCM signal pin
     * }; */
    const esp_video_init_config_t video_config = {
        .dvp = &dvp_config,
    };
    /* Only the DVP device is initialized; the plain esp_video_init() would
     * initialize every video device enabled in sdkconfig (ISP, JPEG, ...). */
    error = esp_video_init_with_flags(&video_config, ESP_VIDEO_INIT_FLAGS_DVP);
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
    /* Tear down only what bsp_camera_start() initialized */
    esp_err_t result = esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP);
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
