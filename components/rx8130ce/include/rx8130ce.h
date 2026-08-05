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

/** I2C and backup supply configuration used when creating a device. */
typedef struct {
    uint8_t device_address;
    uint32_t scl_speed_hz;
    /**
     * Backup battery charge policy applied on create (CHGEN bit, appman
     * 14.7.2). Automatic supply switchover (INIEN) is always enabled.
     * Keep false for a primary (non-rechargeable) backup cell; set true when
     * the board carries a rechargeable backup source (secondary cell or
     * supercapacitor) that must be charged from VDD.
     */
    bool backup_charge_enable;
} rx8130ce_config_t;

/** Default RX8130CE configuration: I2C defaults, backup charging off. */
#define RX8130CE_CONFIG_DEFAULT()                 \
    {                                             \
        .device_address = RX8130CE_I2C_ADDRESS_DEFAULT, \
        .scl_speed_hz = RX8130CE_I2C_CLOCK_HZ,    \
        .backup_charge_enable = false,            \
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

/**
 * Source clock of the fixed-cycle (wake-up) timer.
 *
 * The values match the TSEL2-TSEL0 encoding of the extension register, so
 * they can be applied to the register image directly.
 */
typedef enum {
    RX8130CE_TIMER_SOURCE_4096HZ = 0,   /**< 4096 Hz: 244.14 us per count. */
    RX8130CE_TIMER_SOURCE_64HZ = 1,     /**< 64 Hz: 15.625 ms per count. */
    RX8130CE_TIMER_SOURCE_1HZ = 2,      /**< 1 Hz: 1 s per count. */
    RX8130CE_TIMER_SOURCE_1_60HZ = 3,   /**< 1/60 Hz: 1 min per count. */
    RX8130CE_TIMER_SOURCE_1_3600HZ = 4, /**< 1/3600 Hz: 1 h per count. */
} rx8130ce_timer_source_t;

/**
 * Fixed-cycle (wake-up) timer settings for the RX8130CE.
 *
 * The 16-bit counter counts down from count at the source clock rate and
 * latches TF when it reaches zero; with TIE set the /IRQ pin pulses low for
 * the auto reset time. The period ranges from 244.14 us (one count at
 * 4096 Hz) to 65535 hours.
 */
typedef struct {
    bool enable; /**< Start the countdown (TE=1) or stop the timer (TE=0). */
    rx8130ce_timer_source_t source_clock; /**< Countdown rate (TSEL). */
    uint16_t count; /**< Down-counter preset, 1-65535. */
} rx8130ce_timer_t;

/**
 * Furthest future target rx8130ce_alarm_from_time() accepts, in days.
 *
 * The alarm hardware compares day-of-month, hour, and minute only, so the
 * same compare pattern recurs every month (and day 29-31 patterns can skip
 * short months). A target no more than 27 days out is always the first
 * recurrence of its pattern, because the previous recurrence is at least
 * 28 days earlier; farther targets could match an earlier recurrence.
 */
#define RX8130CE_ALARM_MAX_FUTURE_DAYS 27

/**
 * Power-on / backup-domain health check result.
 *
 * Reported by rx8130ce_check_power(); the driver only reads the flags and
 * derives advice, it does not modify the device.
 */
typedef struct {
    bool voltage_low;      /**< VLF reads 1: register contents were lost. */
    bool reset_detected;   /**< RSF reads 1: a reset-voltage event latched. */
    bool init_recommended; /**< Driver advice: re-initialize and re-set the
                                calendar (mirrors voltage_low, appman 14.5.1:
                                VLF=1 requires initializing all registers). */
} rx8130ce_power_check_t;

/** Return true when a calendar value can be represented by the device. */
bool rx8130ce_time_is_valid(const rx8130ce_time_t *time);

/** Return true when an alarm value can be represented by the device. */
bool rx8130ce_alarm_is_valid(const rx8130ce_alarm_t *alarm);

/**
 * Derive alarm compare settings that fire at an absolute future time.
 *
 * Pure helper for the "wake at a specific moment" use case (for example
 * deep-sleep wake-up on a board without a 32.768 kHz crystal, where the RTC
 * alarm is the only timed wake-up path): read the current time with
 * rx8130ce_get_time(), pick the target, and pass both here.
 *
 * The result compares minute, hour, and day-of-month; the hardware then
 * latches AF (and asserts /IRQ when AIE is set) at the start of the target
 * minute. The target must be at least one minute ahead of now and no more
 * than RX8130CE_ALARM_MAX_FUTURE_DAYS days out, otherwise the call fails
 * with ESP_ERR_INVALID_ARG.
 *
 * @param now Current calendar time; must pass rx8130ce_time_is_valid().
 * @param target Wake-up moment; must pass rx8130ce_time_is_valid() and lie
 *        strictly after now at minute granularity.
 * @param out_alarm Receives the compare settings for rx8130ce_set_alarm().
 */
esp_err_t rx8130ce_alarm_from_time(const rx8130ce_time_t *now,
                                   const rx8130ce_time_t *target,
                                   rx8130ce_alarm_t *out_alarm);

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

/** Return true when a timer value can be represented by the device. */
bool rx8130ce_timer_is_valid(const rx8130ce_timer_t *timer);

/**
 * Encode a timer preset into the raw 1Ah-1Bh register image.
 *
 * @param timer Timer value; must pass rx8130ce_timer_is_valid().
 * @param registers Receives the Timer Counter 0/1 register values, low byte
 *        first.
 * @param tsel_bits Receives the raw TSEL2-TSEL0 bit field for the extension
 *        register.
 */
void rx8130ce_timer_encode(const rx8130ce_timer_t *timer,
                           uint8_t registers[2], uint8_t *tsel_bits);

/**
 * Create a device on an existing I2C bus and verify register access.
 *
 * A NULL config selects RX8130CE_CONFIG_DEFAULT(). The configured backup
 * charge policy is applied after normal power-up and as part of the VLF=1
 * full-register initialization.
 */
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
 * Check the power-on / backup-domain health flags (VLF, RSF).
 *
 * Advisory and read-only: nothing is cleared or reconfigured. When
 * init_recommended comes back true, register contents are untrustworthy;
 * recover by re-setting the calendar with rx8130ce_set_time() (which clears
 * VLF) and re-applying alarm/timer settings. Note that rx8130ce_create()
 * already performs the full initialization when it finds VLF=1, so a true
 * result at runtime means the backup supply dropped out after creation.
 */
esp_err_t rx8130ce_check_power(rx8130ce_handle_t handle,
                               rx8130ce_power_check_t *out_check);

/**
 * Configure backup battery charging at runtime (CHGEN/INIEN, appman 14.7.2).
 *
 * Automatic supply switchover (INIEN=1, the recommended setting) is always
 * kept enabled; enable selects whether VDD also charges the backup source
 * (CHGEN). Keep disabled for a primary (non-rechargeable) backup cell;
 * enable only when the board carries a rechargeable backup source. The
 * default after rx8130ce_create() comes from
 * rx8130ce_config_t.backup_charge_enable (off unless configured otherwise).
 */
esp_err_t rx8130ce_set_backup_charge(rx8130ce_handle_t handle, bool enable);

/** Read back whether backup battery charging is enabled (CHGEN). */
esp_err_t rx8130ce_get_backup_charge(rx8130ce_handle_t handle,
                                     bool *out_enabled);

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

/**
 * Disarm the alarm: gate /IRQ (AIE=0), ignore all compare fields, and clear
 * any latched alarm flag.
 *
 * With every compare field ignored the hardware matches once per minute
 * (appman 14.3.1 note *3), so AIE stays cleared to keep /IRQ released and AF
 * may re-latch afterwards; both are harmless while the alarm is disarmed.
 * Combine with rx8130ce_alarm_irq_enable() to re-arm after
 * rx8130ce_set_alarm().
 */
esp_err_t rx8130ce_clear_alarm(rx8130ce_handle_t handle);

/**
 * Report and clear the latched alarm flag (AF).
 *
 * Selective variant of rx8130ce_get_and_clear_interrupts() for the shared
 * /IRQ line: only AF is cleared, UF/TF and the remaining flags are left
 * untouched. Per the datasheet the flag clears when written 0 and ignores
 * writes of 1, so the read-modify-write used here cannot disturb other
 * flags. alarm_flag may be NULL to just clear.
 */
esp_err_t rx8130ce_get_and_clear_alarm_flag(rx8130ce_handle_t handle,
        bool *alarm_flag);

/**
 * Program the fixed-cycle (wake-up) timer.
 *
 * TE is held cleared while the preset and source clock change, as required by
 * the application manual, and any latched timer flag is cleared. The
 * countdown starts from the preset when timer.enable is true; false leaves
 * the timer stopped. Use rx8130ce_timer_irq_enable() to route the timer event
 * to the /IRQ pin.
 */
esp_err_t rx8130ce_set_timer(rx8130ce_handle_t handle,
                             const rx8130ce_timer_t *timer);

/**
 * Read back the fixed-cycle timer settings.
 *
 * While the timer runs (enable is true), count reads back as the live
 * down-count, which is not latched during the read; read twice until two
 * reads agree, or stop the timer first, for an exact value. count may read
 * back 0 before the timer is first programmed.
 */
esp_err_t rx8130ce_get_timer(rx8130ce_handle_t handle,
                             rx8130ce_timer_t *out_timer);

/** Enable or disable the timer interrupt output on the /IRQ pin (TIE). */
esp_err_t rx8130ce_timer_irq_enable(rx8130ce_handle_t handle, bool enable);

/** Enable or disable the time update interrupt on the /IRQ pin (UIE). */
esp_err_t rx8130ce_update_irq_enable(rx8130ce_handle_t handle, bool enable);

/** Read and clear the update, timer, and alarm interrupt flags. */
esp_err_t rx8130ce_get_and_clear_interrupts(rx8130ce_handle_t handle,
        uint8_t *flags);

#ifdef __cplusplus
}
#endif
