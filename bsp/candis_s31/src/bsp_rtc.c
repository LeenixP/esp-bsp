/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_check.h"

#include "bsp/candis_s31.h"

#define RX8130_REG_SECONDS             0x10
#define RX8130_REG_FLAGS               0x1D
#define RX8130_FLAG_VBLF               (1U << 7)
#define RX8130_FLAG_UF                 (1U << 5)
#define RX8130_FLAG_TF                 (1U << 4)
#define RX8130_FLAG_AF                 (1U << 3)
#define RX8130_FLAG_RSF                (1U << 2)
#define RX8130_FLAG_VLF                (1U << 1)
#define RX8130_FLAG_VBFF               (1U << 0)
#define RX8130_INTERRUPT_FLAGS         (RX8130_FLAG_UF | RX8130_FLAG_TF | RX8130_FLAG_AF)
#define RX8130_TIMEOUT_MS              100

static const char *TAG = "candis_rtc";
static i2c_master_dev_handle_t s_rtc;

static uint8_t bcd_to_binary(uint8_t value)
{
    return (value >> 4) * 10 + (value & 0x0F);
}

static uint8_t binary_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t rtc_read(uint8_t reg, void *data, size_t size)
{
    ESP_RETURN_ON_ERROR(bsp_rtc_init(), TAG, "RX8130CE is unavailable");
    return i2c_master_transmit_receive(s_rtc, &reg, 1, data, size,
                                       RX8130_TIMEOUT_MS);
}

static esp_err_t rtc_write(uint8_t reg, const void *data, size_t size)
{
    ESP_RETURN_ON_ERROR(bsp_rtc_init(), TAG, "RX8130CE is unavailable");
    uint8_t buffer[1 + 7];
    ESP_RETURN_ON_FALSE(size <= sizeof(buffer) - 1, ESP_ERR_INVALID_SIZE, TAG,
                        "RX8130CE write is too large");
    buffer[0] = reg;
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(s_rtc, buffer, size + 1, RX8130_TIMEOUT_MS);
}

static bool rtc_time_is_valid(const bsp_rtc_time_t *time)
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

esp_err_t bsp_rtc_init(void)
{
    if (s_rtc != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = bsp_lp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "low-power I2C init failed");
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_RX8130CE_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &s_rtc);
}

esp_err_t bsp_rtc_deinit(void)
{
    if (s_rtc == NULL) {
        return ESP_OK;
    }
    const esp_err_t error = i2c_master_bus_rm_device(s_rtc);
    if (error == ESP_OK) {
        s_rtc = NULL;
    }
    return error;
}

esp_err_t bsp_rtc_get_status(bsp_rtc_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    uint8_t flags = 0;
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_REG_FLAGS, &flags, 1), TAG,
                        "RX8130CE flag read failed");
    *status = (bsp_rtc_status_t) {
        .raw = flags,
        .time_valid = (flags & RX8130_FLAG_VLF) == 0,
        .alarm = (flags & RX8130_FLAG_AF) != 0,
        .timer = (flags & RX8130_FLAG_TF) != 0,
        .update = (flags & RX8130_FLAG_UF) != 0,
        .reset = (flags & RX8130_FLAG_RSF) != 0,
        .backup_voltage_low = (flags & (RX8130_FLAG_VBLF | RX8130_FLAG_VBFF)) != 0,
    };
    return ESP_OK;
}

esp_err_t bsp_rtc_get_time(bsp_rtc_time_t *time, bsp_rtc_status_t *status)
{
    ESP_RETURN_ON_FALSE(time != NULL, ESP_ERR_INVALID_ARG, TAG, "time is NULL");
    uint8_t data[7] = {0};
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_REG_SECONDS, data, sizeof(data)), TAG,
                        "RX8130CE time read failed");

    uint8_t weekday = 0;
    while (weekday < 7 && (data[3] & (1U << weekday)) == 0) {
        ++weekday;
    }
    *time = (bsp_rtc_time_t) {
        .year = 2000 + bcd_to_binary(data[6]),
        .month = bcd_to_binary(data[5] & 0x1F),
        .day = bcd_to_binary(data[4] & 0x3F),
        .weekday = weekday < 7 ? weekday : 0,
        .hour = bcd_to_binary(data[2] & 0x3F),
        .minute = bcd_to_binary(data[1] & 0x7F),
        .second = bcd_to_binary(data[0] & 0x7F),
    };
    if (status != NULL) {
        return bsp_rtc_get_status(status);
    }
    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(const bsp_rtc_time_t *time)
{
    ESP_RETURN_ON_FALSE(rtc_time_is_valid(time), ESP_ERR_INVALID_ARG, TAG,
                        "invalid calendar time");
    const uint8_t data[7] = {
        binary_to_bcd(time->second),
        binary_to_bcd(time->minute),
        binary_to_bcd(time->hour),
        (uint8_t)(1U << time->weekday),
        binary_to_bcd(time->day),
        binary_to_bcd(time->month),
        binary_to_bcd((uint8_t)(time->year - 2000)),
    };
    ESP_RETURN_ON_ERROR(rtc_write(RX8130_REG_SECONDS, data, sizeof(data)), TAG,
                        "RX8130CE time write failed");

    uint8_t flags = 0;
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_REG_FLAGS, &flags, 1), TAG,
                        "RX8130CE flag read failed");
    flags &= ~RX8130_FLAG_VLF;
    return rtc_write(RX8130_REG_FLAGS, &flags, 1);
}

esp_err_t bsp_rtc_clear_interrupt_flags(uint8_t *flags)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_REG_FLAGS, &value, 1), TAG,
                        "RX8130CE flag read failed");
    if (flags != NULL) {
        *flags = value;
    }
    value &= ~RX8130_INTERRUPT_FLAGS;
    return rtc_write(RX8130_REG_FLAGS, &value, 1);
}
