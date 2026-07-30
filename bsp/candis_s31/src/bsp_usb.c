/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "bsp/candis_s31.h"

#define USB_HOST_TASK_STACK_SIZE 4096
#define USB_HOST_TASK_PRIORITY   5
#define USB_HOST_STOP_TIMEOUT_MS 1000

static const char *TAG = "candis_usb";
static TaskHandle_t s_usb_host_task;
static volatile bool s_stop_requested;
static volatile bool s_task_exited;
static volatile bool s_all_devices_free;

static void usb_host_event_task(void *argument)
{
    (void)argument;
    while (!s_stop_requested) {
        uint32_t event_flags = 0;
        const esp_err_t error = usb_host_lib_handle_events(portMAX_DELAY,
                                &event_flags);
        if (error == ESP_ERR_INVALID_STATE) {
            break;
        }
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "USB Host event handling failed: %s",
                     esp_err_to_name(error));
            continue;
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0) {
            const esp_err_t free_error = usb_host_device_free_all();
            if (free_error == ESP_OK) {
                s_all_devices_free = true;
            }
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0) {
            s_all_devices_free = true;
        }
    }

    s_usb_host_task = NULL;
    s_task_exited = true;
    vTaskDelete(NULL);
}

static esp_err_t usb_host_event_task_start(void)
{
    s_stop_requested = false;
    s_task_exited = false;
    s_all_devices_free = false;
    const BaseType_t created = xTaskCreate(usb_host_event_task, "usb_host_lib",
                                           USB_HOST_TASK_STACK_SIZE, NULL,
                                           USB_HOST_TASK_PRIORITY,
                                           &s_usb_host_task);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t wait_for_flag(volatile bool *flag)
{
    const TickType_t timeout = pdMS_TO_TICKS(USB_HOST_STOP_TIMEOUT_MS);
    const TickType_t start = xTaskGetTickCount();
    while (!*flag) {
        if ((xTaskGetTickCount() - start) >= timeout) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA)
{
    ESP_RETURN_ON_FALSE(mode == BSP_USB_HOST_POWER_MODE_USB_DEV,
                        ESP_ERR_INVALID_ARG, TAG, "invalid USB power mode");
    if (s_usb_host_task != NULL) {
        return ESP_OK;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_config), TAG,
                        "USB Host installation failed");
    esp_err_t error = usb_host_event_task_start();
    if (error != ESP_OK) {
        usb_host_uninstall();
        return error;
    }

    const bsp_type_c_current_t current = limit_500mA ?
                                         BSP_TYPE_C_CURRENT_DEFAULT :
                                         BSP_TYPE_C_CURRENT_1_5_A;
    error = bsp_usb_otg_power_set(true, current);
    if (error != ESP_OK) {
        bsp_usb_host_stop();
        return error;
    }
    return ESP_OK;
}

esp_err_t bsp_usb_host_stop(void)
{
    if (s_usb_host_task == NULL) {
        return bsp_usb_otg_power_set(false, BSP_TYPE_C_CURRENT_DEFAULT);
    }

    const esp_err_t free_error = usb_host_device_free_all();
    if (free_error == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "USB Host clients must be deregistered before stop");
        return free_error;
    }
    if (free_error == ESP_OK) {
        s_all_devices_free = true;
    } else if (free_error != ESP_ERR_NOT_FINISHED) {
        return free_error;
    }
    ESP_RETURN_ON_ERROR(wait_for_flag(&s_all_devices_free), TAG,
                        "USB devices did not become free");

    s_stop_requested = true;
    ESP_RETURN_ON_ERROR(usb_host_lib_unblock(), TAG,
                        "USB Host event task unblock failed");
    ESP_RETURN_ON_ERROR(wait_for_flag(&s_task_exited), TAG,
                        "USB Host event task did not stop");
    ESP_RETURN_ON_ERROR(usb_host_uninstall(), TAG,
                        "USB Host uninstall failed");
    return bsp_usb_otg_power_set(false, BSP_TYPE_C_CURRENT_DEFAULT);
}
