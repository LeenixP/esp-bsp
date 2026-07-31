/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"

#include "tg28_sw.h"

#include "bsp/candis_s31.h"

static const char *TAG = "candis_pmic";
static tg28_sw_handle_t s_pmic;

_Static_assert((int)BSP_PMIC_REGULATOR_COUNT == (int)TG28_SW_REGULATOR_COUNT,
               "BSP and TG28_SW regulator lists must stay aligned");

static tg28_sw_regulator_t to_tg28_regulator(bsp_pmic_regulator_t regulator)
{
    return (tg28_sw_regulator_t)regulator;
}

esp_err_t bsp_pmic_init(void)
{
    if (s_pmic != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_lp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "low-power I2C init failed");

    const tg28_sw_config_t config = {
        .device_address = BSP_TG28_SW_I2C_ADDRESS,
        .scl_speed_hz = TG28_SW_I2C_CLOCK_HZ,
    };
    esp_err_t error = tg28_sw_create(bus, &config, &s_pmic);
    if (error == ESP_OK) {
        /* Clear any latched interrupt status before enabling the power-key
         * IRQs, like the vendor axp-core driver does at irq-chip init
         * (write 1 to clear every pending bit), so stale events from the
         * boot ROM or a previous reset do not fire immediately. */
        uint8_t pending[3] = {0};
        error = tg28_sw_get_and_clear_interrupts(s_pmic, pending);
    }
    if (error == ESP_OK) {
        error = tg28_sw_configure_power_key_interrupts(s_pmic,
                                                       TG28_SW_POWER_KEY_IRQ_ALL);
    }
    if (error == ESP_OK) {
        /* Board-level choice: the Candis-S31 battery has no NTC resistor,
         * so the TS pin is the external fixed input and its current source
         * stays off. The 50uA value is the power-on default and is
         * irrelevant while the current source is off. */
        error = tg28_sw_set_ts_config(s_pmic, TG28_SW_TS_MODE_EXTERNAL_FIXED,
                                      TG28_SW_TS_CURRENT_SOURCE_OFF, 50);
    }
    if (error != ESP_OK && s_pmic != NULL) {
        tg28_sw_delete(s_pmic);
        s_pmic = NULL;
    }
    return error;
}

esp_err_t bsp_pmic_deinit(void)
{
    if (s_pmic == NULL) {
        return ESP_OK;
    }
    const esp_err_t error = tg28_sw_delete(s_pmic);
    if (error == ESP_OK) {
        s_pmic = NULL;
    }
    return error;
}

esp_err_t bsp_pmic_get_status(bsp_pmic_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");

    tg28_sw_status_t device_status = {0};
    ESP_RETURN_ON_ERROR(tg28_sw_get_status(s_pmic, &device_status), TAG,
                        "TG28_SW status read failed");
    status->chip_id = device_status.chip_id;
    status->common_status0 = device_status.common_status0;
    status->common_status1 = device_status.common_status1;
    status->battery_mv = device_status.battery_mv;
    status->battery_percent = device_status.battery_percent;
    status->battery_present = device_status.battery_present;
    status->vbus_present = device_status.vbus_present;
    status->charging = device_status.charging;
    status->charge_done = device_status.charge_done;
    return ESP_OK;
}

esp_err_t bsp_pmic_get_power_on_source(uint8_t *source)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_get_power_on_source(s_pmic, source);
}

esp_err_t bsp_pmic_set_charge_current(uint16_t milliamps)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_set_charge_current(s_pmic, milliamps);
}

esp_err_t bsp_pmic_get_charge_current(uint16_t *milliamps)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_get_charge_current(s_pmic, milliamps);
}

esp_err_t bsp_pmic_set_input_current_limit(uint16_t milliamps)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_set_input_current_limit(s_pmic, milliamps);
}

esp_err_t bsp_pmic_get_input_current_limit(uint16_t *milliamps)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_get_input_current_limit(s_pmic, milliamps);
}

esp_err_t bsp_pmic_set_charge_voltage(uint16_t millivolts)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_set_charge_voltage(s_pmic, millivolts);
}

esp_err_t bsp_pmic_get_charge_voltage(uint16_t *millivolts)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_get_charge_voltage(s_pmic, millivolts);
}

esp_err_t bsp_pmic_program_battery_model(const uint8_t *model, size_t size)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_program_battery_model(s_pmic, model, size);
}

esp_err_t bsp_pmic_regulator_set_voltage(bsp_pmic_regulator_t regulator,
        uint16_t millivolts)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_regulator_set_voltage(s_pmic, to_tg28_regulator(regulator),
                                         millivolts);
}

esp_err_t bsp_pmic_regulator_get_voltage(bsp_pmic_regulator_t regulator,
        uint16_t *millivolts)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_regulator_get_voltage(s_pmic, to_tg28_regulator(regulator),
                                         millivolts);
}

esp_err_t bsp_pmic_regulator_enable(bsp_pmic_regulator_t regulator, bool enable)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_regulator_enable(s_pmic, to_tg28_regulator(regulator), enable);
}

esp_err_t bsp_pmic_regulator_is_enabled(bsp_pmic_regulator_t regulator, bool *enabled)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_regulator_is_enabled(s_pmic, to_tg28_regulator(regulator), enabled);
}

esp_err_t bsp_pmic_get_and_clear_interrupts(uint8_t status[3])
{
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW is unavailable");
    return tg28_sw_get_and_clear_interrupts(s_pmic, status);
}

const char *bsp_pmic_regulator_name(bsp_pmic_regulator_t regulator)
{
    return tg28_sw_regulator_name(to_tg28_regulator(regulator));
}
