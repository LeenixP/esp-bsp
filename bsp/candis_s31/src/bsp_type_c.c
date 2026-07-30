/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/candis_s31.h"

#define FUSB303_REG_DEVICE_ID          0x01
#define FUSB303_REG_DEVICE_TYPE        0x02
#define FUSB303_REG_PORT_ROLE          0x03
#define FUSB303_REG_CONTROL            0x04
#define FUSB303_REG_CONTROL1           0x05
#define FUSB303_REG_MANUAL             0x09
#define FUSB303_REG_STATUS             0x11
#define FUSB303_REG_STATUS1            0x12
#define FUSB303_REG_TYPE               0x13
#define FUSB303_REG_INTERRUPT          0x14
#define FUSB303_CONTROL_INT_MASK       (1U << 0)
#define FUSB303_CONTROL_HOST_CUR_MASK  (3U << 1)
#define FUSB303_CONTROL1_ENABLE        (1U << 3)
#define FUSB303_MANUAL_DISABLED        (1U << 1)
#define FUSB303_DEVICE_TYPE_VALUE      0x03
#define FUSB303_DEVICE_VERSION         0x01
#define FUSB303_TIMEOUT_MS             100

static const char *TAG = "candis_type_c";
static i2c_master_dev_handle_t s_type_c;
static uint8_t s_type_c_address;

static esp_err_t type_c_read(uint8_t reg, void *data, size_t size)
{
    ESP_RETURN_ON_ERROR(bsp_type_c_init(), TAG, "FUSB303B is unavailable");
    return i2c_master_transmit_receive(s_type_c, &reg, 1, data, size,
                                       FUSB303_TIMEOUT_MS);
}

static esp_err_t type_c_write(uint8_t reg, const void *data, size_t size)
{
    ESP_RETURN_ON_ERROR(bsp_type_c_init(), TAG, "FUSB303B is unavailable");
    uint8_t buffer[1 + 2];
    ESP_RETURN_ON_FALSE(size <= sizeof(buffer) - 1, ESP_ERR_INVALID_SIZE, TAG,
                        "FUSB303B write is too large");
    buffer[0] = reg;
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(s_type_c, buffer, size + 1, FUSB303_TIMEOUT_MS);
}

esp_err_t bsp_type_c_init(void)
{
    if (s_type_c != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, true), TAG,
                        "FUSB303B enable failed");
    vTaskDelay(pdMS_TO_TICKS(2));

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "main I2C init failed");
    const uint8_t addresses[] = {
        BSP_FUSB303B_I2C_ADDRESS_LOW,
        BSP_FUSB303B_I2C_ADDRESS_HIGH,
    };
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (size_t index = 0; index < sizeof(addresses); ++index) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[index],
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_type_c), TAG,
                            "cannot add FUSB303B I2C device");
        uint8_t identity[2] = {0};
        last_error = i2c_master_transmit_receive(
        s_type_c, (const uint8_t[]) {
            FUSB303_REG_DEVICE_ID
        }, 1,
        identity, sizeof(identity), FUSB303_TIMEOUT_MS);
        if (last_error == ESP_OK &&
                (identity[0] >> 4) == FUSB303_DEVICE_VERSION &&
                identity[1] == FUSB303_DEVICE_TYPE_VALUE) {
            s_type_c_address = addresses[index];
            uint8_t control1 = 0;
            last_error = type_c_read(FUSB303_REG_CONTROL1, &control1, 1);
            if (last_error == ESP_OK) {
                control1 |= FUSB303_CONTROL1_ENABLE;
                last_error = type_c_write(FUSB303_REG_CONTROL1, &control1, 1);
            }
            uint8_t control = 0;
            if (last_error == ESP_OK) {
                last_error = type_c_read(FUSB303_REG_CONTROL, &control, 1);
            }
            if (last_error == ESP_OK) {
                control &= ~FUSB303_CONTROL_INT_MASK;
                last_error = type_c_write(FUSB303_REG_CONTROL, &control, 1);
            }
            if (last_error == ESP_OK) {
                return ESP_OK;
            }
        }
        if (last_error == ESP_OK) {
            last_error = ESP_ERR_INVALID_RESPONSE;
        }
        i2c_master_bus_rm_device(s_type_c);
        s_type_c = NULL;
        s_type_c_address = 0;
    }
    bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, false);
    return last_error;
}

esp_err_t bsp_type_c_deinit(void)
{
    esp_err_t result = ESP_OK;
    if (s_type_c != NULL) {
        result = i2c_master_bus_rm_device(s_type_c);
        if (result != ESP_OK) {
            return result;
        }
        s_type_c = NULL;
        s_type_c_address = 0;
    }
    const esp_err_t power_error =
        bsp_power_domain_set(BSP_POWER_TYPE_C_CONTROL, false);
    return result != ESP_OK ? result : power_error;
}

esp_err_t bsp_type_c_get_status(bsp_type_c_status_t *status, bool clear_interrupts)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    memset(status, 0, sizeof(*status));
    status->i2c_address = s_type_c_address;
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_DEVICE_ID, &status->device_id, 1),
                        TAG, "device ID read failed");
    status->i2c_address = s_type_c_address;
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_DEVICE_TYPE, &status->device_type, 1),
                        TAG, "device type read failed");
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_STATUS, &status->status, 2), TAG,
                        "status read failed");
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_TYPE, &status->type, 1), TAG,
                        "connection type read failed");
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_INTERRUPT, &status->interrupt, 2),
                        TAG, "interrupt read failed");
    status->attached = (status->status & 0x01) != 0;
    status->vbus_ok = (status->status & (1U << 3)) != 0;
    status->orientation = (status->status >> 4) & 0x03;
    const uint8_t current = (status->status >> 1) & 0x03;
    status->advertised_current = current >= 3 ? BSP_TYPE_C_CURRENT_3_0_A :
                                 current == 2 ? BSP_TYPE_C_CURRENT_1_5_A :
                                 BSP_TYPE_C_CURRENT_DEFAULT;
    if (clear_interrupts) {
        const uint8_t interrupt_status[2] = {status->interrupt, status->interrupt1};
        return type_c_write(FUSB303_REG_INTERRUPT, interrupt_status,
                            sizeof(interrupt_status));
    }
    return ESP_OK;
}

esp_err_t bsp_type_c_set_role(bsp_type_c_role_t role, bsp_type_c_current_t current)
{
    ESP_RETURN_ON_FALSE(role >= BSP_TYPE_C_ROLE_DISABLED && role <= BSP_TYPE_C_ROLE_DRP &&
                        current >= BSP_TYPE_C_CURRENT_DEFAULT &&
                        current <= BSP_TYPE_C_CURRENT_3_0_A,
                        ESP_ERR_INVALID_ARG, TAG, "invalid Type-C role request");
    ESP_RETURN_ON_ERROR(bsp_type_c_init(), TAG, "FUSB303B is unavailable");

    if (role == BSP_TYPE_C_ROLE_DISABLED) {
        const uint8_t manual = FUSB303_MANUAL_DISABLED;
        return type_c_write(FUSB303_REG_MANUAL, &manual, 1);
    }

    const uint8_t manual = 0;
    ESP_RETURN_ON_ERROR(type_c_write(FUSB303_REG_MANUAL, &manual, 1), TAG,
                        "cannot leave disabled state");

    uint8_t port_role = 0;
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_PORT_ROLE, &port_role, 1), TAG,
                        "port role read failed");
    port_role &= (1U << 6) | (1U << 3);
    port_role |= role == BSP_TYPE_C_ROLE_SINK ? (1U << 1) :
                 role == BSP_TYPE_C_ROLE_SOURCE ? (1U << 0) : (1U << 2);
    ESP_RETURN_ON_ERROR(type_c_write(FUSB303_REG_PORT_ROLE, &port_role, 1), TAG,
                        "port role write failed");

    uint8_t control = 0;
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_CONTROL, &control, 1), TAG,
                        "control read failed");
    const uint8_t host_current = current == BSP_TYPE_C_CURRENT_3_0_A ? 3 :
                                 current == BSP_TYPE_C_CURRENT_1_5_A ? 2 : 1;
    control = (control & ~(FUSB303_CONTROL_HOST_CUR_MASK | FUSB303_CONTROL_INT_MASK)) |
              (host_current << 1);
    ESP_RETURN_ON_ERROR(type_c_write(FUSB303_REG_CONTROL, &control, 1), TAG,
                        "control write failed");

    uint8_t control1 = 0;
    ESP_RETURN_ON_ERROR(type_c_read(FUSB303_REG_CONTROL1, &control1, 1), TAG,
                        "control1 read failed");
    control1 |= FUSB303_CONTROL1_ENABLE;
    return type_c_write(FUSB303_REG_CONTROL1, &control1, 1);
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
