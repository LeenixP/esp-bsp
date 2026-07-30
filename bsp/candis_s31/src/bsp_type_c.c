/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fusb303b.h"

#include "bsp/candis_s31.h"

static const char *TAG = "candis_type_c";
static fusb303b_handle_t s_type_c;

static fusb303b_current_t current_to_driver(bsp_type_c_current_t current)
{
    return current == BSP_TYPE_C_CURRENT_3_0_A ? FUSB303B_CURRENT_3_0_A :
           current == BSP_TYPE_C_CURRENT_1_5_A ? FUSB303B_CURRENT_1_5_A :
           FUSB303B_CURRENT_DEFAULT;
}

static fusb303b_role_t role_to_driver(bsp_type_c_role_t role)
{
    return role == BSP_TYPE_C_ROLE_SINK ? FUSB303B_ROLE_SINK :
           role == BSP_TYPE_C_ROLE_SOURCE ? FUSB303B_ROLE_SOURCE :
           role == BSP_TYPE_C_ROLE_DRP ? FUSB303B_ROLE_DRP :
           FUSB303B_ROLE_DISABLED;
}

esp_err_t bsp_type_c_init(void)
{
    if (s_type_c != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, true), TAG,
                        "FUSB303B enable failed");
    vTaskDelay(pdMS_TO_TICKS(FUSB303B_ENABLE_TO_I2C_DELAY_MS));

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, false);
        return ESP_FAIL;
    }
    const uint8_t addresses[] = {
        BSP_FUSB303B_I2C_ADDRESS_LOW,
        BSP_FUSB303B_I2C_ADDRESS_HIGH,
    };
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (size_t index = 0; index < sizeof(addresses) / sizeof(addresses[0]); ++index) {
        fusb303b_config_t config = FUSB303B_CONFIG_DEFAULT();
        config.device_address = addresses[index];
        last_error = fusb303b_create(bus, &config, &s_type_c);
        if (last_error == ESP_OK) {
            last_error = fusb303b_set_enabled(s_type_c, true);
        }
        if (last_error == ESP_OK) {
            last_error = fusb303b_set_global_interrupt_mask(s_type_c, false);
        }
        if (last_error == ESP_OK) {
            return ESP_OK;
        }
        if (s_type_c != NULL) {
            fusb303b_delete(s_type_c);
            s_type_c = NULL;
        }
    }
    bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, false);
    return last_error;
}

esp_err_t bsp_type_c_deinit(void)
{
    if (s_type_c != NULL) {
        const esp_err_t error = fusb303b_delete(s_type_c);
        if (error != ESP_OK) {
            return error;
        }
        s_type_c = NULL;
    }
    return bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, false);
}

esp_err_t bsp_type_c_get_status(bsp_type_c_status_t *status, bool clear_interrupts)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    ESP_RETURN_ON_ERROR(bsp_type_c_init(), TAG, "FUSB303B is unavailable");
    fusb303b_status_t driver_status;
    ESP_RETURN_ON_ERROR(fusb303b_get_status(s_type_c, &driver_status,
                                            clear_interrupts), TAG,
                        "FUSB303B status read failed");
    *status = (bsp_type_c_status_t) {
        .i2c_address = driver_status.i2c_address,
        .device_id = driver_status.device_id,
        .device_type = driver_status.device_type,
        .status = driver_status.status,
        .status1 = driver_status.status1,
        .type = driver_status.type,
        .interrupt = driver_status.interrupt,
        .interrupt1 = driver_status.interrupt1,
        .attached = driver_status.attached,
        .vbus_ok = driver_status.vbus_ok,
        .orientation = driver_status.orientation,
        .advertised_current = driver_status.advertised_current == FUSB303B_CURRENT_3_0_A ?
                              BSP_TYPE_C_CURRENT_3_0_A :
                              driver_status.advertised_current == FUSB303B_CURRENT_1_5_A ?
                              BSP_TYPE_C_CURRENT_1_5_A : BSP_TYPE_C_CURRENT_DEFAULT,
    };
    return ESP_OK;
}

esp_err_t bsp_type_c_set_role(bsp_type_c_role_t role, bsp_type_c_current_t current)
{
    ESP_RETURN_ON_FALSE(role >= BSP_TYPE_C_ROLE_DISABLED &&
                        role <= BSP_TYPE_C_ROLE_DRP &&
                        current >= BSP_TYPE_C_CURRENT_DEFAULT &&
                        current <= BSP_TYPE_C_CURRENT_3_0_A,
                        ESP_ERR_INVALID_ARG, TAG, "invalid Type-C role request");
    ESP_RETURN_ON_ERROR(bsp_type_c_init(), TAG, "FUSB303B is unavailable");
    return fusb303b_set_role(s_type_c, role_to_driver(role),
                             current_to_driver(current));
}

esp_err_t bsp_usb_otg_power_set(bool enable, bsp_type_c_current_t current)
{
    if (!enable) {
        ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_USB_OTG, false), TAG,
                            "OTG boost disable failed");
        const esp_err_t role_error = s_type_c != NULL ?
                                     bsp_type_c_set_role(BSP_TYPE_C_ROLE_DISABLED,
                                             BSP_TYPE_C_CURRENT_DEFAULT) : ESP_OK;
        const esp_err_t control_error = bsp_type_c_deinit();
        return role_error != ESP_OK ? role_error : control_error;
    }

    ESP_RETURN_ON_ERROR(bsp_type_c_set_role(BSP_TYPE_C_ROLE_SOURCE, current), TAG,
                        "source role setup failed");
    return bsp_power_domain_set(BSP_POWER_USB_OTG, true);
}
