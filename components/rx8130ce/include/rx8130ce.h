/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Driver for the Epson RX8130CE real-time clock.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RX8130CE_I2C_ADDRESS_DEFAULT 0x32
#define RX8130CE_I2C_CLOCK_HZ        400000

#define RX8130CE_FLAG_VBLF           (1U << 7)
#define RX8130CE_FLAG_UF             (1U << 5)
#define RX8130CE_FLAG_TF             (1U << 4)
#define RX8130CE_FLAG_AF             (1U << 3)
#define RX8130CE_FLAG_RSF            (1U << 2)
#define RX8130CE_FLAG_VLF            (1U << 1)
#define RX8130CE_FLAG_VBFF           (1U << 0)

/** Opaque RX8130CE device handle. */
typedef struct rx8130ce_device_t *rx8130ce_handle_t;

/** I2C configuration used when creating an RX8130CE device. */
typedef struct {
    uint8_t device_address;
    uint32_t scl_speed_hz;
} rx8130ce_config_t;

/** Default RX8130CE I2C configuration. */
#define RX8130CE_CONFIG_DEFAULT()                 \
    {                                             \
        .device_address = RX8130CE_I2C_ADDRESS_DEFAULT, \
        .scl_speed_hz = RX8130CE_I2C_CLOCK_HZ,    \
    }

/** Calendar time represented by the RX8130CE. */
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rx8130ce_time_t;

/** Decoded status from the RX8130CE flag register. */
typedef struct {
    uint8_t raw;
    bool time_valid;
    bool alarm;
    bool timer;
    bool update;
    bool reset;
    bool backup_voltage_low;
    bool backup_battery_full;
} rx8130ce_status_t;

/**
 * Alarm compare settings for the RX8130CE.
 *
 * Each *_en flag selects whether the field participates in the alarm
 * comparison; it maps to the active-low AE bit of the alarm register.
 * Fields with a cleared *_en flag are ignored. The hardware compares either
 * the day of month or the weekday, never both, so day_en and weekday_en are
 * mutually exclusive.
 */
typedef struct {
    bool minute_en;  /**< Compare the minute field. */
    uint8_t minute;  /**< Minute, 0-59. */
    bool hour_en;    /**< Compare the hour field. */
    uint8_t hour;    /**< Hour, 0-23. */
    bool day_en;     /**< Compare the day-of-month field. */
    uint8_t day;     /**< Day of month, 1-31. */
    bool weekday_en; /**< Compare the weekday field. */
    uint8_t weekday; /**< Weekday, 0 (Sunday) - 6 (Saturday). */
} rx8130ce_alarm_t;

/** Return true when a calendar value can be represented by the device. */
bool rx8130ce_time_is_valid(const rx8130ce_time_t *time);

/** Return true when an alarm value can be represented by the device. */
bool rx8130ce_alarm_is_valid(const rx8130ce_alarm_t *alarm);

/**
 * Encode an alarm into the raw 17h-19h register image.
 *
 * @param alarm Alarm value; must pass rx8130ce_alarm_is_valid().
 * @param registers Receives the MIN/HOUR/WEEK-DAY alarm register values
 *        with the active-low AE bits applied.
 * @param use_day_alarm Receives the required WADA bit state: true selects
 *        the day-of-month register layout, false the weekday layout.
 */
void rx8130ce_alarm_encode(const rx8130ce_alarm_t *alarm,
                           uint8_t registers[3], bool *use_day_alarm);

/** Create a device on an existing I2C bus and verify register access. */
esp_err_t rx8130ce_create(i2c_master_bus_handle_t bus,
                          const rx8130ce_config_t *config,
                          rx8130ce_handle_t *ret_handle);

/** Delete the device and remove it from the I2C bus. */
esp_err_t rx8130ce_delete(rx8130ce_handle_t handle);

/** Read the calendar and optionally return the retained status flags. */
esp_err_t rx8130ce_get_time(rx8130ce_handle_t handle,
                            rx8130ce_time_t *time,
                            rx8130ce_status_t *status);

/** Set the calendar while the time counter is protected by the STOP bit. */
esp_err_t rx8130ce_set_time(rx8130ce_handle_t handle,
                            const rx8130ce_time_t *time);

/** Read and decode the flag register without clearing it. */
esp_err_t rx8130ce_get_status(rx8130ce_handle_t handle,
                              rx8130ce_status_t *status);

/**
 * Program the alarm compare registers.
 *
 * AIE is held cleared while the registers change, as recommended by the
 * application manual, and any latched alarm flag is cleared before the
 * previous AIE state is restored. Use rx8130ce_alarm_irq_enable() to route
 * the alarm event to the /IRQ pin.
 */
esp_err_t rx8130ce_set_alarm(rx8130ce_handle_t handle,
                             const rx8130ce_alarm_t *alarm);

/** Read back the alarm compare registers. */
esp_err_t rx8130ce_get_alarm(rx8130ce_handle_t handle,
                             rx8130ce_alarm_t *out_alarm);

/** Enable or disable the alarm interrupt output on the /IRQ pin (AIE). */
esp_err_t rx8130ce_alarm_irq_enable(rx8130ce_handle_t handle, bool enable);

/** Read and clear the update, timer, and alarm interrupt flags. */
esp_err_t rx8130ce_get_and_clear_interrupts(rx8130ce_handle_t handle,
        uint8_t *flags);

#ifdef __cplusplus
}
#endif
