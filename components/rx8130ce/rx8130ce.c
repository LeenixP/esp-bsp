/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"

#include "rx8130ce.h"

#define RX8130CE_REG_SECONDS          0x10
#define RX8130CE_REG_FLAGS            0x1D
#define RX8130CE_REG_CONTROL0         0x1E
#define RX8130CE_CONTROL0_STOP        (1U << 6)
#define RX8130CE_INTERRUPT_FLAGS      (RX8130CE_FLAG_UF | RX8130CE_FLAG_TF | RX8130CE_FLAG_AF)
#define RX8130CE_TIMEOUT_MS           100

struct rx8130ce_device_t {
    i2c_master_dev_handle_t i2c_device;
};

static const char *TAG = "rx8130ce";

static esp_err_t rx8130ce_read(rx8130ce_handle_t handle, uint8_t reg,
                               void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(handle != NULL && handle->i2c_device != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid device handle");
    ESP_RETURN_ON_FALSE(data != NULL && size > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid read buffer");
    return i2c_master_transmit_receive(handle->i2c_device, &reg, 1, data,
                                       size, RX8130CE_TIMEOUT_MS);
}

static esp_err_t rx8130ce_write(rx8130ce_handle_t handle, uint8_t reg,
                                const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(handle != NULL && handle->i2c_device != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid device handle");
    ESP_RETURN_ON_FALSE(data != NULL && size > 0 && size <= 7,
                        ESP_ERR_INVALID_ARG, TAG, "invalid write buffer");
    uint8_t buffer[8];
    buffer[0] = reg;
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(handle->i2c_device, buffer, size + 1,
                               RX8130CE_TIMEOUT_MS);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static uint8_t binary_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool bcd_is_valid(uint8_t value)
{
    return (value & 0x0F) <= 9 && (value >> 4) <= 9;
}

bool rx8130ce_time_is_valid(const rx8130ce_time_t *time)
{
    if (time == NULL || time->year < 2000 || time->year > 2099 ||
            time->month < 1 || time->month > 12 || time->weekday > 6 ||
            time->hour > 23 || time->minute > 59 || time->second > 59) {
        return false;
    }
    static const uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    uint8_t days = days_per_month[time->month - 1];
    if (time->month == 2 && (time->year % 4) == 0) {
        ++days;
    }
    return time->day >= 1 && time->day <= days;
}

static esp_err_t decode_time(const uint8_t data[7], rx8130ce_time_t *time)
{
    const uint8_t second = data[0] & 0x7F;
    const uint8_t minute = data[1] & 0x7F;
    const uint8_t hour = data[2] & 0x3F;
    const uint8_t weekday_bits = data[3] & 0x7F;
    const uint8_t day = data[4] & 0x3F;
    const uint8_t month = data[5] & 0x1F;
    const uint8_t year = data[6];

    ESP_RETURN_ON_FALSE(bcd_is_valid(second) && bcd_is_valid(minute) &&
                        bcd_is_valid(hour) && bcd_is_valid(day) &&
                        bcd_is_valid(month) && bcd_is_valid(year),
                        ESP_ERR_INVALID_RESPONSE, TAG, "invalid BCD calendar data");
    ESP_RETURN_ON_FALSE(weekday_bits != 0 &&
                        (weekday_bits & (weekday_bits - 1U)) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG, "invalid weekday data");

    uint8_t weekday = 0;
    while ((weekday_bits & (1U << weekday)) == 0) {
        ++weekday;
    }
    *time = (rx8130ce_time_t) {
        .year = (uint16_t)(2000 + bcd_to_binary(year)),
        .month = bcd_to_binary(month),
        .day = bcd_to_binary(day),
        .weekday = weekday,
        .hour = bcd_to_binary(hour),
        .minute = bcd_to_binary(minute),
        .second = bcd_to_binary(second),
    };
    ESP_RETURN_ON_FALSE(rx8130ce_time_is_valid(time), ESP_ERR_INVALID_RESPONSE,
                        TAG, "calendar value is out of range");
    return ESP_OK;
}

static void decode_status(uint8_t flags, rx8130ce_status_t *status)
{
    *status = (rx8130ce_status_t) {
        .raw = flags,
        .time_valid = (flags & RX8130CE_FLAG_VLF) == 0,
        .alarm = (flags & RX8130CE_FLAG_AF) != 0,
        .timer = (flags & RX8130CE_FLAG_TF) != 0,
        .update = (flags & RX8130CE_FLAG_UF) != 0,
        .reset = (flags & RX8130CE_FLAG_RSF) != 0,
        .backup_voltage_low = (flags & (RX8130CE_FLAG_VBLF |
                                        RX8130CE_FLAG_VBFF)) != 0,
    };
}

esp_err_t rx8130ce_create(i2c_master_bus_handle_t bus,
                          const rx8130ce_config_t *config,
                          rx8130ce_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && ret_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid create argument");
    ESP_RETURN_ON_FALSE(config->device_address <= 0x7F &&
                        config->scl_speed_hz > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid I2C configuration");
    *ret_handle = NULL;

    rx8130ce_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, TAG,
                        "device allocation failed");
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->device_address,
        .scl_speed_hz = config->scl_speed_hz,
    };
    esp_err_t error = i2c_master_bus_add_device(bus, &device_config,
                      &handle->i2c_device);
    if (error == ESP_OK) {
        uint8_t flags = 0;
        error = rx8130ce_read(handle, RX8130CE_REG_FLAGS, &flags, 1);
    }
    if (error != ESP_OK) {
        if (handle->i2c_device != NULL) {
            i2c_master_bus_rm_device(handle->i2c_device);
        }
        free(handle);
        return error;
    }
    *ret_handle = handle;
    return ESP_OK;
}

esp_err_t rx8130ce_delete(rx8130ce_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL && handle->i2c_device != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid device handle");
    const esp_err_t error = i2c_master_bus_rm_device(handle->i2c_device);
    if (error == ESP_OK) {
        free(handle);
    }
    return error;
}

esp_err_t rx8130ce_get_status(rx8130ce_handle_t handle,
                              rx8130ce_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "status is NULL");
    uint8_t flags = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_FLAGS, &flags, 1),
                        TAG, "flag read failed");
    decode_status(flags, status);
    return ESP_OK;
}

esp_err_t rx8130ce_get_time(rx8130ce_handle_t handle,
                            rx8130ce_time_t *time,
                            rx8130ce_status_t *status)
{
    ESP_RETURN_ON_FALSE(time != NULL, ESP_ERR_INVALID_ARG, TAG, "time is NULL");
    uint8_t data[7] = {0};
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_SECONDS, data,
                                      sizeof(data)), TAG, "calendar read failed");
    ESP_RETURN_ON_ERROR(decode_time(data, time), TAG,
                        "calendar decode failed");
    return status != NULL ? rx8130ce_get_status(handle, status) : ESP_OK;
}

esp_err_t rx8130ce_set_time(rx8130ce_handle_t handle,
                            const rx8130ce_time_t *time)
{
    ESP_RETURN_ON_FALSE(rx8130ce_time_is_valid(time), ESP_ERR_INVALID_ARG,
                        TAG, "invalid calendar time");
    const uint8_t data[7] = {
        binary_to_bcd(time->second),
        binary_to_bcd(time->minute),
        binary_to_bcd(time->hour),
        (uint8_t)(1U << time->weekday),
        binary_to_bcd(time->day),
        binary_to_bcd(time->month),
        binary_to_bcd((uint8_t)(time->year - 2000)),
    };

    uint8_t control0 = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_CONTROL0,
                                      &control0, 1), TAG,
                        "control register read failed");
    const uint8_t stopped_control0 = control0 | RX8130CE_CONTROL0_STOP;
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_CONTROL0,
                                       &stopped_control0, 1), TAG,
                        "time counter stop failed");

    esp_err_t error = rx8130ce_write(handle, RX8130CE_REG_SECONDS, data,
                                     sizeof(data));
    if (error == ESP_OK) {
        uint8_t flags = 0;
        error = rx8130ce_read(handle, RX8130CE_REG_FLAGS, &flags, 1);
        if (error == ESP_OK) {
            flags &= ~RX8130CE_FLAG_VLF;
            error = rx8130ce_write(handle, RX8130CE_REG_FLAGS, &flags, 1);
        }
    }

    const esp_err_t restart_error = rx8130ce_write(handle,
                                    RX8130CE_REG_CONTROL0,
                                    &control0, 1);
    return error != ESP_OK ? error : restart_error;
}

esp_err_t rx8130ce_get_and_clear_interrupts(rx8130ce_handle_t handle,
        uint8_t *flags)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_FLAGS, &value, 1),
                        TAG, "flag read failed");
    if (flags != NULL) {
        *flags = value;
    }
    value &= ~RX8130CE_INTERRUPT_FLAGS;
    return rx8130ce_write(handle, RX8130CE_REG_FLAGS, &value, 1);
}
