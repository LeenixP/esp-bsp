/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rx8130ce.h"

#define RX8130CE_REG_SECONDS          0x10
#define RX8130CE_REG_ALARM_MINUTE     0x17
#define RX8130CE_REG_EXTENSION        0x1C
#define RX8130CE_REG_FLAGS            0x1D
#define RX8130CE_REG_CONTROL0         0x1E
#define RX8130CE_REG_CONTROL1         0x1F
#define RX8130CE_REG_RAM              0x20
#define RX8130CE_REG_DIGITAL_OFFSET   0x30
#define RX8130CE_ALARM_AE             (1U << 7)
#define RX8130CE_EXTENSION_WADA       (1U << 3)
#define RX8130CE_CONTROL0_STOP        (1U << 6)
#define RX8130CE_CONTROL0_AIE         (1U << 3)
#define RX8130CE_CONTROL1_CHGEN       (1U << 5)
#define RX8130CE_CONTROL1_INIEN       (1U << 4)
#define RX8130CE_INTERRUPT_FLAGS      (RX8130CE_FLAG_UF | RX8130CE_FLAG_TF | RX8130CE_FLAG_AF)
#define RX8130CE_TIMEOUT_MS           100
#define RX8130CE_BACKUP_RECOVERY_MS   35
/* Oscillation start time t_str is 1.0 s max (appman 9.1); appman 10.2
 * requires waiting it out before initializing when VLF=1. */
#define RX8130CE_OSCILLATOR_START_MS  1100

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

bool rx8130ce_alarm_is_valid(const rx8130ce_alarm_t *alarm)
{
    if (alarm == NULL || (alarm->minute_en && alarm->minute > 59) ||
            (alarm->hour_en && alarm->hour > 23) ||
            (alarm->day_en && (alarm->day < 1 || alarm->day > 31)) ||
            (alarm->weekday_en && alarm->weekday > 6)) {
        return false;
    }
    /* Day and weekday share register 19h, so they cannot both compare. */
    return !(alarm->day_en && alarm->weekday_en);
}

void rx8130ce_alarm_encode(const rx8130ce_alarm_t *alarm,
                           uint8_t registers[3], bool *use_day_alarm)
{
    /* AE is active-low: a set bit excludes the field from the comparison. */
    registers[0] = alarm->minute_en ? binary_to_bcd(alarm->minute)
                   : RX8130CE_ALARM_AE;
    registers[1] = alarm->hour_en ? binary_to_bcd(alarm->hour)
                   : RX8130CE_ALARM_AE;
    if (alarm->day_en) {
        registers[2] = binary_to_bcd(alarm->day);
    } else if (alarm->weekday_en) {
        registers[2] = (uint8_t)(1U << alarm->weekday);
    } else {
        registers[2] = RX8130CE_ALARM_AE;
    }
    *use_day_alarm = alarm->day_en;
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

static esp_err_t decode_alarm(const uint8_t data[3], bool use_day_alarm,
                              rx8130ce_alarm_t *alarm)
{
    *alarm = (rx8130ce_alarm_t) {
        .minute_en = (data[0] & RX8130CE_ALARM_AE) == 0,
        .minute = bcd_to_binary(data[0] & 0x7F),
        .hour_en = (data[1] & RX8130CE_ALARM_AE) == 0,
        .hour = bcd_to_binary(data[1] & 0x3F),
    };
    ESP_RETURN_ON_FALSE(bcd_is_valid(data[0] & 0x7F) &&
                        bcd_is_valid(data[1] & 0x3F),
                        ESP_ERR_INVALID_RESPONSE, TAG, "invalid BCD alarm data");
    if (use_day_alarm) {
        ESP_RETURN_ON_FALSE(bcd_is_valid(data[2] & 0x3F),
                            ESP_ERR_INVALID_RESPONSE, TAG,
                            "invalid BCD alarm data");
        alarm->day_en = (data[2] & RX8130CE_ALARM_AE) == 0;
        alarm->day = bcd_to_binary(data[2] & 0x3F);
    } else {
        const uint8_t weekday_bits = data[2] & 0x7F;
        alarm->weekday_en = (data[2] & RX8130CE_ALARM_AE) == 0 &&
                            weekday_bits != 0;
        /* The hardware accepts a weekday mask; report the lowest set day. */
        while (alarm->weekday < 6 &&
                (weekday_bits & (1U << alarm->weekday)) == 0) {
            ++alarm->weekday;
        }
    }
    ESP_RETURN_ON_FALSE(rx8130ce_alarm_is_valid(alarm),
                        ESP_ERR_INVALID_RESPONSE, TAG,
                        "alarm value is out of range");
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
        .backup_voltage_low = (flags & RX8130CE_FLAG_VBLF) != 0,
        .backup_battery_full = (flags & RX8130CE_FLAG_VBFF) != 0,
    };
}

static esp_err_t rx8130ce_configure_primary_backup(rx8130ce_handle_t handle)
{
    uint8_t control1 = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_CONTROL1,
                                      &control1, 1), TAG,
                        "backup control read failed");
    control1 &= ~RX8130CE_CONTROL1_CHGEN;
    control1 |= RX8130CE_CONTROL1_INIEN;
    return rx8130ce_write(handle, RX8130CE_REG_CONTROL1, &control1, 1);
}

static esp_err_t rx8130ce_initialize_all_registers(rx8130ce_handle_t handle)
{
    /* Known-safe epoch: 2000-01-01 00:00:00, Saturday. */
    static const uint8_t calendar[7] = {
        0x00, 0x00, 0x00, 1U << 6, 0x01, 0x01, 0x00,
    };
    static const uint8_t alarm_timer_extension[6] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
    };
    static const uint8_t ram[4] = {0};
    const uint8_t control0_stopped = RX8130CE_CONTROL0_STOP;
    const uint8_t control0_running = 0;
    const uint8_t flags = 0;
    const uint8_t control1 = RX8130CE_CONTROL1_INIEN;
    const uint8_t digital_offset = 0;

    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_CONTROL0,
                                       &control0_stopped, 1), TAG,
                        "initial counter stop failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_SECONDS,
                                       calendar, sizeof(calendar)), TAG,
                        "initial calendar write failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_ALARM_MINUTE,
                                       alarm_timer_extension,
                                       sizeof(alarm_timer_extension)), TAG,
                        "alarm/timer initialization failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_FLAGS,
                                       &flags, 1), TAG,
                        "flag initialization failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_CONTROL1,
                                       &control1, 1), TAG,
                        "backup control initialization failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_RAM,
                                       ram, sizeof(ram)), TAG,
                        "RAM initialization failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_DIGITAL_OFFSET,
                                       &digital_offset, 1), TAG,
                        "digital offset initialization failed");
    return rx8130ce_write(handle, RX8130CE_REG_CONTROL0,
                          &control0_running, 1);
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
        /* Covers the specified t_int after return from backup operation. */
        vTaskDelay(pdMS_TO_TICKS(RX8130CE_BACKUP_RECOVERY_MS));
        uint8_t flags = 0;
        error = rx8130ce_read(handle, RX8130CE_REG_FLAGS, &flags, 1);
        if (error == ESP_OK && (flags & RX8130CE_FLAG_VLF) != 0) {
            /* Appman 10.2: with VLF=1, initialize only after waiting out
             * the oscillator start time t_str (1.0 s max, appman 9.1). */
            vTaskDelay(pdMS_TO_TICKS(RX8130CE_OSCILLATOR_START_MS));
            error = rx8130ce_initialize_all_registers(handle);
        } else if (error == ESP_OK) {
            /* Primary backup cell: enable switchover, never enable charging. */
            error = rx8130ce_configure_primary_backup(handle);
        }
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

esp_err_t rx8130ce_set_alarm(rx8130ce_handle_t handle,
                             const rx8130ce_alarm_t *alarm)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid device handle");
    ESP_RETURN_ON_FALSE(rx8130ce_alarm_is_valid(alarm), ESP_ERR_INVALID_ARG,
                        TAG, "invalid alarm");
    uint8_t registers[3];
    bool use_day_alarm = false;
    rx8130ce_alarm_encode(alarm, registers, &use_day_alarm);

    uint8_t control0 = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_CONTROL0,
                                      &control0, 1), TAG,
                        "control register read failed");
    /* Appman 14.3.1 recommends holding AIE cleared while settings change. */
    const uint8_t disabled_control0 = control0 & ~RX8130CE_CONTROL0_AIE;
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_CONTROL0,
                                       &disabled_control0, 1), TAG,
                        "alarm interrupt disable failed");
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_ALARM_MINUTE,
                                       registers, sizeof(registers)), TAG,
                        "alarm register write failed");

    uint8_t extension = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_EXTENSION,
                                      &extension, 1), TAG,
                        "extension register read failed");
    if (use_day_alarm) {
        extension |= RX8130CE_EXTENSION_WADA;
    } else {
        extension &= ~RX8130CE_EXTENSION_WADA;
    }
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_EXTENSION,
                                       &extension, 1), TAG,
                        "alarm target select failed");

    /* Drop any alarm event latched while the registers changed. */
    uint8_t flags = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_FLAGS, &flags, 1),
                        TAG, "flag read failed");
    flags &= ~RX8130CE_FLAG_AF;
    ESP_RETURN_ON_ERROR(rx8130ce_write(handle, RX8130CE_REG_FLAGS, &flags, 1),
                        TAG, "alarm flag clear failed");
    return rx8130ce_write(handle, RX8130CE_REG_CONTROL0, &control0, 1);
}

esp_err_t rx8130ce_get_alarm(rx8130ce_handle_t handle,
                             rx8130ce_alarm_t *out_alarm)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid device handle");
    ESP_RETURN_ON_FALSE(out_alarm != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "alarm is NULL");
    uint8_t data[3] = {0};
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_ALARM_MINUTE,
                                      data, sizeof(data)), TAG,
                        "alarm read failed");
    uint8_t extension = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_EXTENSION,
                                      &extension, 1), TAG,
                        "extension register read failed");
    return decode_alarm(data, (extension & RX8130CE_EXTENSION_WADA) != 0,
                        out_alarm);
}

esp_err_t rx8130ce_alarm_irq_enable(rx8130ce_handle_t handle, bool enable)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid device handle");
    uint8_t control0 = 0;
    ESP_RETURN_ON_ERROR(rx8130ce_read(handle, RX8130CE_REG_CONTROL0,
                                      &control0, 1), TAG,
                        "control register read failed");
    if (enable) {
        control0 |= RX8130CE_CONTROL0_AIE;
    } else {
        control0 &= ~RX8130CE_CONTROL0_AIE;
    }
    return rx8130ce_write(handle, RX8130CE_REG_CONTROL0, &control0, 1);
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
