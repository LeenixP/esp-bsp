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
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TG28_SW_I2C_ADDRESS_DEFAULT  0x34
#define TG28_SW_I2C_CLOCK_HZ         400000

#define TG28_SW_POWER_KEY_IRQ_POSITIVE_EDGE  (1U << 0)
#define TG28_SW_POWER_KEY_IRQ_NEGATIVE_EDGE  (1U << 1)
#define TG28_SW_POWER_KEY_IRQ_LONG_PRESS      (1U << 2)
#define TG28_SW_POWER_KEY_IRQ_SHORT_PRESS     (1U << 3)
#define TG28_SW_POWER_KEY_IRQ_ALL             0x0F

/** Opaque TG28 switch-charger device handle. */
typedef struct tg28_sw_device_t *tg28_sw_handle_t;

/** TG28 regulators exposed by this driver. */
typedef enum {
    TG28_SW_DCDC1 = 0,
    TG28_SW_DCDC2,
    TG28_SW_DCDC3,
    TG28_SW_DCDC4,
    TG28_SW_ALDO1,
    TG28_SW_ALDO2,
    TG28_SW_ALDO3,
    TG28_SW_ALDO4,
    TG28_SW_BLDO1,
    TG28_SW_BLDO2,
    TG28_SW_DLDO1,
    TG28_SW_DLDO2,
    TG28_SW_REGULATOR_COUNT,
} tg28_sw_regulator_t;

/** I2C configuration used when creating a TG28 device. */
typedef struct {
    uint8_t device_address;
    uint32_t scl_speed_hz;
    /** Optional battery-specific model downloaded through REGA1 on create. */
    const uint8_t *battery_model;
    size_t battery_model_size;
} tg28_sw_config_t;

/** Default TG28 switch-charger I2C configuration. */
#define TG28_SW_CONFIG_DEFAULT()                    \
    {                                               \
        .device_address = TG28_SW_I2C_ADDRESS_DEFAULT, \
        .scl_speed_hz = TG28_SW_I2C_CLOCK_HZ,       \
        .battery_model = NULL,                      \
        .battery_model_size = 0,                    \
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

/** Read the raw power-on source bitmap from REG20. */
esp_err_t tg28_sw_get_power_on_source(tg28_sw_handle_t handle, uint8_t *source);

/** Explicitly configure the four REG41 power-key interrupt enable bits. */
esp_err_t tg28_sw_configure_power_key_interrupts(tg28_sw_handle_t handle,
        uint8_t enabled_mask);

/** Select the board's externally fixed TS input and disable its current source. */
esp_err_t tg28_sw_configure_external_fixed_ts(tg28_sw_handle_t handle);

/** Set an exactly representable REG62 constant-current charge limit. */
esp_err_t tg28_sw_set_charge_current(tg28_sw_handle_t handle, uint16_t milliamps);

/** Read the REG62 constant-current charge limit. */
esp_err_t tg28_sw_get_charge_current(tg28_sw_handle_t handle, uint16_t *milliamps);

/**
 * Set the REG16 input current limit. Only the discrete vendor levels
 * 100/500/900/1000/1500/2000 mA are accepted; anything else returns
 * ESP_ERR_INVALID_ARG.
 */
esp_err_t tg28_sw_set_input_current_limit(tg28_sw_handle_t handle,
        uint16_t milliamps);

/** Read the REG16 input current limit. */
esp_err_t tg28_sw_get_input_current_limit(tg28_sw_handle_t handle,
        uint16_t *milliamps);

/**
 * Set the REG64 charge termination voltage. Only the discrete vendor levels
 * 3900/4000/4100/4200/4350/4400 mV are accepted; anything else returns
 * ESP_ERR_INVALID_ARG.
 */
esp_err_t tg28_sw_set_charge_voltage(tg28_sw_handle_t handle,
                                     uint16_t millivolts);

/** Read the REG64 charge termination voltage. */
esp_err_t tg28_sw_get_charge_voltage(tg28_sw_handle_t handle,
                                     uint16_t *millivolts);

/**
 * Set the REG15 VINDPM threshold to an exactly representable value
 * (3880 + 80 * code, codes 2-10, i.e. 4040-4680 mV in 80 mV steps);
 * anything else returns ESP_ERR_INVALID_ARG.
 */
esp_err_t tg28_sw_set_vindpm(tg28_sw_handle_t handle, uint16_t millivolts);

/** Read the REG15 VINDPM threshold. */
esp_err_t tg28_sw_get_vindpm(tg28_sw_handle_t handle, uint16_t *millivolts);

/**
 * Download and verify a battery-specific fuel-gauge model through REGA1.
 *
 * The sequence follows the vendor reference driver: temporarily disable the
 * charger, reset the gauge MCU, open BROM, stream the model, reopen BROM for
 * verification, set the update mark, reset the gauge MCU, and restore the
 * original charger-enable state. Call once per boot, or pass the model in the
 * create configuration to have the driver do so automatically.
 */
esp_err_t tg28_sw_program_battery_model(tg28_sw_handle_t handle,
                                        const uint8_t *model, size_t size);

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
