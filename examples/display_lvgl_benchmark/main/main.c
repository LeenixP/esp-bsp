/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief BSP LVGL Benchmark Example
 * @details Run LVGL benchmark tests
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lv_demos.h"
#include "bsp/esp-bsp.h"

static char *TAG = "app_main";

#define LOG_MEM_INFO    (0)

void benchmark_end_cb(const lv_demo_benchmark_summary_t *summary)
{
    for (const lv_demo_benchmark_scene_dsc_t *scene = summary->scenes;
            scene->create_cb != NULL; ++scene) {
        const uint32_t count = scene->measurement_cnt;
        ESP_LOGI(TAG, "BENCH scene=%s fps=%lu cpu=%lu render_ms=%lu flush_ms=%lu samples=%lu",
                 scene->name,
                 (unsigned long)(count ? scene->fps_avg / count : 0),
                 (unsigned long)(count ? scene->cpu_avg_usage / count : 0),
                 (unsigned long)(count ? scene->render_avg_time / count : 0),
                 (unsigned long)(count ? scene->flush_avg_time / count : 0),
                 (unsigned long)count);
    }
    const int32_t scenes = summary->valid_scene_cnt;
    ESP_LOGI(TAG, "BENCH total fps=%ld cpu=%ld render_ms=%ld flush_ms=%ld scenes=%ld",
             (long)(scenes ? summary->total_avg_fps / scenes : 0),
             (long)(scenes ? summary->total_avg_cpu / scenes : 0),
             (long)(scenes ? summary->total_avg_render_time / scenes : 0),
             (long)(scenes ? summary->total_avg_flush_time / scenes : 0),
             (long)scenes);
    lv_demo_benchmark_summary_display(summary);
    ESP_LOGI(TAG, "LVGL demo ended");
}

void app_main(void)
{
    /* Initialize display and LVGL */
#if defined(BSP_BOARD_ESP32_S3_LCD_EV_BOARD)
    /* Only for esp32_s3_lcd_ev_board */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    };
    cfg.lvgl_port_cfg.task_stack = 10000;
    bsp_display_start_with_config(&cfg);
#elif defined(BSP_BOARD_ESP_BOX_3)
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = 1,
#else
        .double_buffer = 0,
#endif
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
    cfg.lvgl_port_cfg.task_stack = 10000;
    bsp_display_start_with_config(&cfg);
#elif defined(BSP_BOARD_ESP32_S3_EYE) || defined(BSP_BOARD_M5STACK_CORE_S3)
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = 1,
#else
        .double_buffer = 0,
#endif
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
    cfg.lvgl_port_cfg.task_stack = 10000;
    bsp_display_start_with_config(&cfg);
#elif defined(BSP_BOARD_M5DIAL)
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
    cfg.lvgl_port_cfg.task_stack = 10000;
    bsp_display_start_with_config(&cfg);
#else
    bsp_display_start();
#endif

    /* Use a visible but non-maximum level for the benchmark. */
    ESP_ERROR_CHECK(bsp_display_brightness_set(60));

    lv_demo_benchmark_set_end_cb(benchmark_end_cb);

    ESP_LOGI(TAG, "Display LVGL demo");
    bsp_display_lock(0);
    lv_demo_benchmark();    /* A demo to measure the performance of LVGL or to compare different settings. */
    bsp_display_unlock();
}
