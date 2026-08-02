/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_check.h"

#include "bsp/candis_s31.h"

#define SHARED_IRQ_MAX_SERVICE_PASSES 8

static const char *TAG = "candis_irq";
static bsp_shared_irq_callback_t s_callback;
static bool s_gpio_ready;
static bool s_handler_added;

static void shared_irq_gpio_isr(void *arg)
{
    /* The line is shared, wire-ORed and level-active: mask it on entry so a
     * held-low line cannot re-trigger forever. The task re-arms the
     * interrupt at the end of bsp_shared_irq_service(), once both I2C
     * devices behind the line have been drained. */
    gpio_intr_disable(BSP_PMIC_RTC_INT);
    if (s_callback != NULL) {
        s_callback(arg);
    }
}

static esp_err_t shared_irq_gpio_init(void)
{
    if (s_gpio_ready) {
        return ESP_OK;
    }
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << BSP_PMIC_RTC_INT,
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "shared IRQ GPIO setup failed");
    s_gpio_ready = true;
    return ESP_OK;
}

/* Unmask the level interrupt again after servicing, but only while a
 * callback is registered; the ISR masks it on every notification. */
static void shared_irq_rearm(void)
{
    if (s_callback != NULL) {
        gpio_intr_enable(BSP_PMIC_RTC_INT);
    }
}

esp_err_t bsp_shared_irq_service(bsp_shared_irq_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    memset(status, 0, sizeof(*status));

    ESP_RETURN_ON_ERROR(shared_irq_gpio_init(), TAG,
                        "shared IRQ GPIO setup failed");

    for (unsigned pass = 0; pass < SHARED_IRQ_MAX_SERVICE_PASSES; ++pass) {
        uint8_t pmic[3] = {0};
        uint8_t rtc = 0;
        ESP_RETURN_ON_ERROR(bsp_pmic_get_and_clear_interrupts(pmic), TAG,
                            "TG28_SW interrupt service failed");
        ESP_RETURN_ON_ERROR(bsp_rtc_clear_interrupt_flags(&rtc), TAG,
                            "RX8130CE interrupt service failed");
        for (size_t index = 0; index < sizeof(pmic); ++index) {
            status->pmic[index] |= pmic[index];
        }
        status->rtc |= rtc;
        status->service_passes = pass + 1;
        if (gpio_get_level(BSP_PMIC_RTC_INT) != BSP_PMIC_RTC_INT_ACTIVE_LEVEL) {
            status->line_released = true;
            shared_irq_rearm();
            return ESP_OK;
        }
    }

    /* The line is still asserted after all passes: something remains
     * pending, so re-arming notifies the task again right away. */
    shared_irq_rearm();
    return ESP_ERR_TIMEOUT;
}

esp_err_t bsp_shared_irq_register_callback(bsp_shared_irq_callback_t cb,
        void *arg)
{
    ESP_RETURN_ON_ERROR(shared_irq_gpio_init(), TAG,
                        "shared IRQ GPIO setup failed");

    if (cb == NULL) {
        s_callback = NULL;
        ESP_RETURN_ON_ERROR(gpio_set_intr_type(BSP_PMIC_RTC_INT,
                                               GPIO_INTR_DISABLE), TAG,
                            "shared IRQ interrupt disable failed");
        if (s_handler_added) {
            s_handler_added = false;
            gpio_isr_handler_remove(BSP_PMIC_RTC_INT);
        }
        return ESP_OK;
    }

    /* The application may already own the GPIO ISR service. */
    const esp_err_t service_error = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(service_error == ESP_OK ||
                        service_error == ESP_ERR_INVALID_STATE,
                        service_error, TAG, "GPIO ISR service unavailable");
    /* Re-registration replaces the previous handler instead of failing. */
    if (s_handler_added) {
        s_handler_added = false;
        gpio_isr_handler_remove(BSP_PMIC_RTC_INT);
    }
    /* Level-triggered: a source that asserts while the other still holds the
     * line low produces no new edge, so edge triggering would lose it. The
     * ISR masks the line; bsp_shared_irq_service() re-arms it. */
    s_callback = cb;
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(BSP_PMIC_RTC_INT,
                                           GPIO_INTR_LOW_LEVEL), TAG,
                        "shared IRQ interrupt setup failed");
    const esp_err_t error = gpio_isr_handler_add(BSP_PMIC_RTC_INT,
                            shared_irq_gpio_isr, arg);
    if (error == ESP_OK) {
        s_handler_added = true;
    } else {
        s_callback = NULL;
    }
    return error;
}
