/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Driver for the onsemi FUSB303B USB Type-C port controller.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUSB303B_I2C_ADDRESS_LOW     0x21
#define FUSB303B_I2C_ADDRESS_HIGH    0x31
#define FUSB303B_I2C_CLOCK_HZ        400000
#define FUSB303B_DEVICE_TYPE_VALUE   0x03
#define FUSB303B_DEVICE_VERSION      0x01
/** Minimum delay from active-low EN assertion to the first I2C access. */
#define FUSB303B_ENABLE_TO_I2C_DELAY_MS 100

/** Opaque FUSB303B device handle. */
typedef struct fusb303b_device_t *fusb303b_handle_t;

/** I2C configuration used when creating a FUSB303B device. */
typedef struct {
    uint8_t device_address;
    uint32_t scl_speed_hz;
} fusb303b_config_t;

/** Default FUSB303B I2C configuration for an ADDR/ORIENT-low device. */
#define FUSB303B_CONFIG_DEFAULT()                \
    {                                            \
        .device_address = FUSB303B_I2C_ADDRESS_LOW, \
        .scl_speed_hz = FUSB303B_I2C_CLOCK_HZ,   \
    }

/** USB Type-C power role selected in the Portrole register. */
typedef enum {
    FUSB303B_ROLE_DISABLED = 0,
    FUSB303B_ROLE_SINK,
    FUSB303B_ROLE_SOURCE,
    FUSB303B_ROLE_DRP,
} fusb303b_role_t;

/** Source current advertisement selected in the Control register. */
typedef enum {
    FUSB303B_CURRENT_DEFAULT = 0,
    FUSB303B_CURRENT_1_5_A,
    FUSB303B_CURRENT_3_0_A,
} fusb303b_current_t;

/** Connection and interrupt snapshot. */
typedef struct {
    uint8_t i2c_address;
    uint8_t device_id;
    uint8_t device_type;
    uint8_t status;
    uint8_t status1;
    uint8_t type;
    uint8_t interrupt;
    uint8_t interrupt1;
    bool attached;
    bool vbus_ok;
    bool vbus_safe_0v;
    bool fault;
    bool remedy_active;
    uint8_t orientation;
    fusb303b_current_t advertised_current;
} fusb303b_status_t;

/** Return true when the identity registers describe a FUSB303B. */
bool fusb303b_is_supported_identity(uint8_t device_id, uint8_t device_type);

/** Create a device on an existing I2C bus and verify its identity. */
esp_err_t fusb303b_create(i2c_master_bus_handle_t bus,
                          const fusb303b_config_t *config,
                          fusb303b_handle_t *ret_handle);

/** Delete the device and remove it from the I2C bus. */
esp_err_t fusb303b_delete(fusb303b_handle_t handle);

/** Enable or disable the autonomous Type-C state machine. */
esp_err_t fusb303b_set_enabled(fusb303b_handle_t handle, bool enabled);

/** Set or clear the global interrupt mask. */
esp_err_t fusb303b_set_global_interrupt_mask(fusb303b_handle_t handle,
        bool masked);

/** Select the power role and source current advertisement. */
esp_err_t fusb303b_set_role(fusb303b_handle_t handle,
                            fusb303b_role_t role,
                            fusb303b_current_t current);

/** Read connection state and optionally clear all reported interrupts. */
esp_err_t fusb303b_get_status(fusb303b_handle_t handle,
                              fusb303b_status_t *status,
                              bool clear_interrupts);

/** Read and clear both write-one-to-clear interrupt registers. */
esp_err_t fusb303b_get_and_clear_interrupts(fusb303b_handle_t handle,
        uint8_t *interrupt,
        uint8_t *interrupt1);

#ifdef __cplusplus
}
#endif
