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

#if defined(BSP_BOARD_CANDIS_S31)
#define CANDIS_TE_WAIT_TIMEOUT_MS 25
#define CANDIS_CACHE_LINE_BYTES 64U
#define CANDIS_FULL_BUFFER_BYTES (BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t))
#define CANDIS_FULL_BUFFER_PIXELS \
    ((((CANDIS_FULL_BUFFER_BYTES + CANDIS_CACHE_LINE_BYTES - 1U) / \
        CANDIS_CACHE_LINE_BYTES) * CANDIS_CACHE_LINE_BYTES) / sizeof(uint16_t))

static SemaphoreHandle_t s_te_sem;
static uint32_t s_te_wait_count;
static uint64_t s_te_wait_total_us;

static void IRAM_ATTR candis_te_isr(void *arg)
{
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &need_yield);
    if (need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t candis_te_gate_init(void)
{
    s_te_sem = xSemaphoreCreateBinary();
    if (s_te_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BSP_LCD_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    return gpio_isr_handler_add(BSP_LCD_TE, candis_te_isr, s_te_sem);
}

static void candis_te_wait_cb(lv_event_t *event)
{
    (void)event;
    while (xSemaphoreTake(s_te_sem, 0) == pdTRUE) {
    }

    const int64_t start_us = esp_timer_get_time();
    if (xSemaphoreTake(s_te_sem, pdMS_TO_TICKS(CANDIS_TE_WAIT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "TE falling-edge wait timed out");
        return;
    }

    s_te_wait_total_us += (uint64_t)(esp_timer_get_time() - start_us);
    ++s_te_wait_count;
    if ((s_te_wait_count % 120U) == 0U) {
        ESP_LOGI(TAG, "TE gate waits=%lu avg_wait_us=%llu",
                 (unsigned long)s_te_wait_count,
                 (unsigned long long)(s_te_wait_total_us / s_te_wait_count));
    }
}
#endif

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
#elif defined(BSP_BOARD_CANDIS_S31)
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        /* LVGL's S31 PPA backend requires the backing allocation size, not
         * only its address, to be cache-line aligned. The extra 16 pixels are
         * padding; the logical draw buffer remains 460x460. */
        .buffer_size = CANDIS_FULL_BUFFER_PIXELS,
        .double_buffer = true,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };
    cfg.lvgl_port_cfg.task_stack = 10000;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    ESP_ERROR_CHECK(display != NULL ? ESP_OK : ESP_FAIL);
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_FULL);
    ESP_ERROR_CHECK(candis_te_gate_init());
    lv_display_add_event_cb(display, candis_te_wait_cb, LV_EVENT_FLUSH_START, NULL);
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
