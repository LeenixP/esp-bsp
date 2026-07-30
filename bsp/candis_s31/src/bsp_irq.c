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

esp_err_t bsp_shared_irq_service(bsp_shared_irq_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    memset(status, 0, sizeof(*status));

    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << BSP_PMIC_RTC_INT,
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "shared IRQ GPIO setup failed");

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
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}
