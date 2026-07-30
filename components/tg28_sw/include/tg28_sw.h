/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Driver for the switch-charger variant of the TG28 PMIC.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TG28_SW_I2C_ADDRESS_DEFAULT  0x34
#define TG28_SW_I2C_CLOCK_HZ         400000

/** Opaque TG28 switch-charger device handle. */
typedef struct tg28_sw_device_t *tg28_sw_handle_t;

/** TG28 regulators exposed by this driver. */
typedef enum {
    TG28_SW_DCDC1 = 0,
    TG28_SW_DCDC2,
    TG28_SW_DCDC3,
    TG28_SW_DCDC4,
    TG28_SW_DCDC5,
    TG28_SW_ALDO1,
    TG28_SW_ALDO2,
    TG28_SW_ALDO3,
    TG28_SW_ALDO4,
    TG28_SW_BLDO1,
    TG28_SW_BLDO2,
    TG28_SW_REGULATOR_COUNT,
} tg28_sw_regulator_t;

/** I2C configuration used when creating a TG28 device. */
typedef struct {
    uint8_t device_address;
    uint32_t scl_speed_hz;
} tg28_sw_config_t;

/** Default TG28 switch-charger I2C configuration. */
#define TG28_SW_CONFIG_DEFAULT()                    \
    {                                               \
        .device_address = TG28_SW_I2C_ADDRESS_DEFAULT, \
        .scl_speed_hz = TG28_SW_I2C_CLOCK_HZ,       \
    }

/** Power, battery, and charger snapshot. */
typedef struct {
    uint8_t chip_id;
    uint8_t common_status0;
    uint8_t common_status1;
    uint16_t battery_mv;
    uint8_t battery_percent;
    bool battery_present;
    bool vbus_present;
    bool charging;
    bool charge_done;
} tg28_sw_status_t;

/**
 * @brief Create a TG28 switch-charger device on an existing I2C bus.
 *
 * The I2C bus remains owned by the caller. The driver verifies that the chip
 * ID register can be read but accepts unknown revisions so EVT hardware can
 * still be diagnosed.
 */
esp_err_t tg28_sw_create(i2c_master_bus_handle_t bus,
                         const tg28_sw_config_t *config,
                         tg28_sw_handle_t *ret_handle);

/** Delete a TG28 device and remove it from the I2C bus. */
esp_err_t tg28_sw_delete(tg28_sw_handle_t handle);

/** Read the chip ID register. */
esp_err_t tg28_sw_get_chip_id(tg28_sw_handle_t handle, uint8_t *chip_id);

/** Return true for chip IDs recognized by the vendor driver. */
bool tg28_sw_is_supported_chip_id(uint8_t chip_id);

/** Read power, battery, and charger state. */
esp_err_t tg28_sw_get_status(tg28_sw_handle_t handle, tg28_sw_status_t *status);

/** Set one regulator to an exactly representable voltage. */
esp_err_t tg28_sw_regulator_set_voltage(tg28_sw_handle_t handle,
                                        tg28_sw_regulator_t regulator,
                                        uint16_t millivolts);

/** Read the programmed voltage of one regulator. */
esp_err_t tg28_sw_regulator_get_voltage(tg28_sw_handle_t handle,
                                        tg28_sw_regulator_t regulator,
                                        uint16_t *millivolts);

/** Enable or disable one regulator without changing its voltage. */
esp_err_t tg28_sw_regulator_enable(tg28_sw_handle_t handle,
                                   tg28_sw_regulator_t regulator,
                                   bool enable);

/** Read the enable state of one regulator. */
esp_err_t tg28_sw_regulator_is_enabled(tg28_sw_handle_t handle,
                                       tg28_sw_regulator_t regulator,
                                       bool *enabled);

/** Read and clear the three interrupt status registers. */
esp_err_t tg28_sw_get_and_clear_interrupts(tg28_sw_handle_t handle,
        uint8_t status[3]);

/** Return a stable lowercase regulator name. */
const char *tg28_sw_regulator_name(tg28_sw_regulator_t regulator);

#ifdef __cplusplus
}
#endif
