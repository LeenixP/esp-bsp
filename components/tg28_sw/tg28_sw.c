/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "tg28_sw.h"

#define TG28_SW_REG_COMMON_STATUS0       0x00
#define TG28_SW_REG_CHIP_ID              0x03
#define TG28_SW_REG_VINDPM               0x15
#define TG28_SW_REG_INPUT_CURRENT_LIMIT  0x16
#define TG28_SW_REG_MODE                 0x17
#define TG28_SW_REG_MODULE_ENABLE        0x18
#define TG28_SW_REG_POWER_ON_SOURCE      0x20
#define TG28_SW_REG_VBAT_H               0x34
#define TG28_SW_REG_INT_ENABLE1          0x41
#define TG28_SW_REG_INT_STATUS1          0x48
#define TG28_SW_REG_TS_CONFIG            0x50
#define TG28_SW_REG_CHARGE_CURRENT       0x62
#define TG28_SW_REG_CHARGE_VOLTAGE       0x64
#define TG28_SW_REG_DCDC_ENABLE          0x80
#define TG28_SW_REG_DCDC1_VOLTAGE        0x82
#define TG28_SW_REG_DCDC2_VOLTAGE        0x83
#define TG28_SW_REG_DCDC3_VOLTAGE        0x84
#define TG28_SW_REG_DCDC4_VOLTAGE        0x85
#define TG28_SW_REG_LDO_ENABLE0          0x90
#define TG28_SW_REG_LDO_ENABLE1          0x91
#define TG28_SW_REG_ALDO1_VOLTAGE        0x92
#define TG28_SW_REG_ALDO2_VOLTAGE        0x93
#define TG28_SW_REG_ALDO3_VOLTAGE        0x94
#define TG28_SW_REG_ALDO4_VOLTAGE        0x95
#define TG28_SW_REG_BLDO1_VOLTAGE        0x96
#define TG28_SW_REG_BLDO2_VOLTAGE        0x97
#define TG28_SW_REG_DLDO1_VOLTAGE        0x99
#define TG28_SW_REG_DLDO2_VOLTAGE        0x9A
#define TG28_SW_REG_BATTERY_MODEL        0xA1
#define TG28_SW_REG_FUEL_GAUGE_CONTROL   0xA2
#define TG28_SW_REG_SOC                  0xA4

#define TG28_SW_VBUS_PRESENT_MASK        (1U << 5)
#define TG28_SW_BATTERY_PRESENT_MASK     (1U << 3)
#define TG28_SW_CHARGE_STATE_MASK        0x07
#define TG28_SW_CHARGE_STATE_DONE        0x04
#define TG28_SW_CHARGER_ENABLE_MASK      (1U << 1)
#define TG28_SW_GAUGE_MCU_RESET_MASK     (1U << 2)
#define TG28_SW_POWER_KEY_IRQ_MASK       0x0F
#define TG28_SW_TS_EXTERNAL_FIXED_MASK   (1U << 4)
#define TG28_SW_TS_CURRENT_ENABLE_MASK   (3U << 2)
#define TG28_SW_CHARGE_CURRENT_MASK      0x1F
#define TG28_SW_INPUT_CURRENT_LIMIT_MASK 0x07
#define TG28_SW_CHARGE_VOLTAGE_MASK      0x07
#define TG28_SW_VINDPM_MASK              0x1F
#define TG28_SW_BROM_UPDATE_MARK_MASK    (1U << 4)
#define TG28_SW_BROM_WRITER_ENABLE_MASK  (1U << 0)
#define TG28_SW_REGISTER_TIMEOUT_MS      100
#define TG28_SW_CHARGER_SETTLE_MS        1000
#define TG28_SW_WRITE_BUFFER_SIZE        4

typedef struct {
    const char *name;
    uint8_t voltage_register;
    uint8_t enable_register;
    uint8_t enable_mask;
    uint8_t voltage_mask;
    uint16_t minimum_mv;
    uint16_t maximum_mv;
    uint16_t step_mv;
    uint16_t second_range_start_mv;
    uint16_t second_step_mv;
} regulator_config_t;

struct tg28_sw_device_t {
    i2c_master_dev_handle_t i2c_device;
    SemaphoreHandle_t lock;
};

static const char *TAG = "tg28_sw";

static const regulator_config_t s_regulators[TG28_SW_REGULATOR_COUNT] = {
    [TG28_SW_DCDC1] = {"dcdc1", TG28_SW_REG_DCDC1_VOLTAGE, TG28_SW_REG_DCDC_ENABLE,
        1U << 0, 0x1F, 1500, 3400, 100, 0, 0
    },
    [TG28_SW_DCDC2] = {"dcdc2", TG28_SW_REG_DCDC2_VOLTAGE, TG28_SW_REG_DCDC_ENABLE,
        1U << 1, 0x7F, 500, 1540, 10, 1200, 20
    },
    [TG28_SW_DCDC3] = {"dcdc3", TG28_SW_REG_DCDC3_VOLTAGE, TG28_SW_REG_DCDC_ENABLE,
        1U << 2, 0x7F, 500, 1540, 10, 1200, 20
    },
    [TG28_SW_DCDC4] = {"dcdc4", TG28_SW_REG_DCDC4_VOLTAGE, TG28_SW_REG_DCDC_ENABLE,
        1U << 3, 0x7F, 500, 1840, 10, 1200, 20
    },
    [TG28_SW_ALDO1] = {"aldo1", TG28_SW_REG_ALDO1_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 0, 0x1F, 500, 3500, 100, 0, 0
    },
    [TG28_SW_ALDO2] = {"aldo2", TG28_SW_REG_ALDO2_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 1, 0x1F, 500, 3500, 100, 0, 0
    },
    [TG28_SW_ALDO3] = {"aldo3", TG28_SW_REG_ALDO3_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 2, 0x1F, 500, 3500, 100, 0, 0
    },
    /* The vendor linear-range table for ALDO4 (second segment 3400/200mV)
     * makes 3500mV unreachable. The single 500-3500mV / 100mV range used
     * here matches the vendor decode for every code (code 30 = 3500mV,
     * code 31 clamps to 3500mV) and keeps the full range settable. */
    [TG28_SW_ALDO4] = {"aldo4", TG28_SW_REG_ALDO4_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 3, 0x1F, 500, 3500, 100, 0, 0
    },
    [TG28_SW_BLDO1] = {"bldo1", TG28_SW_REG_BLDO1_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 4, 0x1F, 500, 3500, 100, 0, 0
    },
    [TG28_SW_BLDO2] = {"bldo2", TG28_SW_REG_BLDO2_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 5, 0x1F, 500, 3500, 100, 0, 0
    },
    /* DLDO1/DLDO2 correspond to the vendor driver's LDO10/LDO11. */
    [TG28_SW_DLDO1] = {"dldo1", TG28_SW_REG_DLDO1_VOLTAGE, TG28_SW_REG_LDO_ENABLE0,
        1U << 7, 0x1F, 500, 3500, 100, 0, 0
    },
    [TG28_SW_DLDO2] = {"dldo2", TG28_SW_REG_DLDO2_VOLTAGE, TG28_SW_REG_LDO_ENABLE1,
        1U << 0, 0x1F, 500, 1400, 50, 0, 0
    },
};

static bool regulator_is_valid(tg28_sw_regulator_t regulator)
{
    return regulator >= 0 && regulator < TG28_SW_REGULATOR_COUNT;
}

static esp_err_t lock_device(tg28_sw_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL && handle->i2c_device != NULL && handle->lock != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid device handle");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(handle->lock, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "device lock failed");
    return ESP_OK;
}

static void unlock_device(tg28_sw_handle_t handle)
{
    xSemaphoreGive(handle->lock);
}

static esp_err_t read_registers(tg28_sw_handle_t handle, uint8_t reg,
                                void *data, size_t size)
{
    return i2c_master_transmit_receive(handle->i2c_device, &reg, sizeof(reg),
                                       data, size, TG28_SW_REGISTER_TIMEOUT_MS);
}

static esp_err_t write_registers(tg28_sw_handle_t handle, uint8_t reg,
                                 const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(size <= TG28_SW_WRITE_BUFFER_SIZE - 1,
                        ESP_ERR_INVALID_SIZE, TAG, "register write is too large");
    uint8_t buffer[TG28_SW_WRITE_BUFFER_SIZE] = {reg};
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(handle->i2c_device, buffer, size + 1,
                               TG28_SW_REGISTER_TIMEOUT_MS);
}

static esp_err_t update_bits(tg28_sw_handle_t handle, uint8_t reg,
                             uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    ESP_RETURN_ON_ERROR(read_registers(handle, reg, &current, sizeof(current)),
                        TAG, "register read failed");
    current = (current & ~mask) | (value & mask);
    return write_registers(handle, reg, &current, sizeof(current));
}

static esp_err_t reset_gauge_mcu(tg28_sw_handle_t handle)
{
    ESP_RETURN_ON_ERROR(update_bits(handle, TG28_SW_REG_MODE,
                                    TG28_SW_GAUGE_MCU_RESET_MASK,
                                    TG28_SW_GAUGE_MCU_RESET_MASK),
                        TAG, "fuel-gauge MCU reset assert failed");
    return update_bits(handle, TG28_SW_REG_MODE,
                       TG28_SW_GAUGE_MCU_RESET_MASK, 0);
}

static esp_err_t set_brom_writer(tg28_sw_handle_t handle, bool enable)
{
    return update_bits(handle, TG28_SW_REG_FUEL_GAUGE_CONTROL,
                       TG28_SW_BROM_WRITER_ENABLE_MASK,
                       enable ? TG28_SW_BROM_WRITER_ENABLE_MASK : 0);
}

/* The encode/decode helpers below are intentionally non-static so the
 * test app can exercise the pure conversion logic without I2C hardware. */
esp_err_t tg28_sw_encode_charge_current(uint16_t milliamps, uint8_t *code)
{
    ESP_RETURN_ON_FALSE(code != NULL, ESP_ERR_INVALID_ARG, TAG, "code is NULL");
    uint16_t value = 0;
    if (milliamps <= 200) {
        ESP_RETURN_ON_FALSE(milliamps % 25 == 0, ESP_ERR_INVALID_ARG,
                            TAG, "charge current is not representable");
        value = milliamps / 25;
    } else {
        ESP_RETURN_ON_FALSE(milliamps >= 300 && milliamps <= 1500 &&
                            milliamps % 100 == 0,
                            ESP_ERR_INVALID_ARG, TAG,
                            "charge current is not representable");
        value = 8 + (milliamps - 200) / 100;
    }
    *code = (uint8_t)value;
    return ESP_OK;
}

uint16_t tg28_sw_decode_charge_current(uint8_t code)
{
    code &= TG28_SW_CHARGE_CURRENT_MASK;
    return code <= 8 ? (uint16_t)code * 25 :
           (uint16_t)(200 + (code - 8) * 100);
}

/* REG16 input current limit levels (mA) indexed by the 3-bit code. */
static const uint16_t s_input_current_limits[] = {100, 500, 900, 1000, 1500, 2000};
#define TG28_SW_INPUT_CURRENT_LIMIT_COUNT \
    (sizeof(s_input_current_limits) / sizeof(s_input_current_limits[0]))

esp_err_t tg28_sw_encode_input_current_limit(uint16_t milliamps, uint8_t *code)
{
    ESP_RETURN_ON_FALSE(code != NULL, ESP_ERR_INVALID_ARG, TAG, "code is NULL");
    for (uint8_t i = 0; i < TG28_SW_INPUT_CURRENT_LIMIT_COUNT; ++i) {
        if (s_input_current_limits[i] == milliamps) {
            *code = i;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "input current limit is not representable");
    return ESP_ERR_INVALID_ARG;
}

uint16_t tg28_sw_decode_input_current_limit(uint8_t code)
{
    code &= TG28_SW_INPUT_CURRENT_LIMIT_MASK;
    return code < TG28_SW_INPUT_CURRENT_LIMIT_COUNT ?
           s_input_current_limits[code] : 0;
}

/* REG64 charge termination voltage levels (mV) indexed by the 3-bit code. */
static const uint16_t s_charge_voltages[] = {3900, 4000, 4100, 4200, 4350, 4400};
#define TG28_SW_CHARGE_VOLTAGE_COUNT \
    (sizeof(s_charge_voltages) / sizeof(s_charge_voltages[0]))

esp_err_t tg28_sw_encode_charge_voltage(uint16_t millivolts, uint8_t *code)
{
    ESP_RETURN_ON_FALSE(code != NULL, ESP_ERR_INVALID_ARG, TAG, "code is NULL");
    for (uint8_t i = 0; i < TG28_SW_CHARGE_VOLTAGE_COUNT; ++i) {
        if (s_charge_voltages[i] == millivolts) {
            *code = i;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "charge voltage is not representable");
    return ESP_ERR_INVALID_ARG;
}

uint16_t tg28_sw_decode_charge_voltage(uint8_t code)
{
    code &= TG28_SW_CHARGE_VOLTAGE_MASK;
    return code < TG28_SW_CHARGE_VOLTAGE_COUNT ? s_charge_voltages[code] : 0;
}

/* REG15 VINDPM: millivolts = 3880 + 80 * code. The vendor driver accepts
 * 4000-4700mV, which lands on codes 2-10 (4040-4680mV). */
#define TG28_SW_VINDPM_BASE_MV    3880
#define TG28_SW_VINDPM_STEP_MV    80
#define TG28_SW_VINDPM_MIN_CODE   2
#define TG28_SW_VINDPM_MAX_CODE   10

esp_err_t tg28_sw_encode_vindpm(uint16_t millivolts, uint8_t *code)
{
    ESP_RETURN_ON_FALSE(code != NULL, ESP_ERR_INVALID_ARG, TAG, "code is NULL");
    ESP_RETURN_ON_FALSE(millivolts >= TG28_SW_VINDPM_BASE_MV &&
                        (millivolts - TG28_SW_VINDPM_BASE_MV) %
                        TG28_SW_VINDPM_STEP_MV == 0,
                        ESP_ERR_INVALID_ARG, TAG,
                        "VINDPM voltage is not representable");
    const uint16_t value =
        (millivolts - TG28_SW_VINDPM_BASE_MV) / TG28_SW_VINDPM_STEP_MV;
    ESP_RETURN_ON_FALSE(value >= TG28_SW_VINDPM_MIN_CODE &&
                        value <= TG28_SW_VINDPM_MAX_CODE,
                        ESP_ERR_INVALID_ARG, TAG,
                        "VINDPM voltage is out of range");
    *code = (uint8_t)value;
    return ESP_OK;
}

uint16_t tg28_sw_decode_vindpm(uint8_t code)
{
    code &= TG28_SW_VINDPM_MASK;
    return TG28_SW_VINDPM_BASE_MV + (uint16_t)code * TG28_SW_VINDPM_STEP_MV;
}

static esp_err_t encode_voltage(const regulator_config_t *config,
                                uint16_t millivolts, uint8_t *code)
{
    if (millivolts < config->minimum_mv || millivolts > config->maximum_mv) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t encoded = 0;
    if (config->second_range_start_mv != 0 &&
            millivolts > config->second_range_start_mv) {
        const uint16_t first_codes =
            (config->second_range_start_mv - config->minimum_mv) / config->step_mv;
        if ((millivolts - config->second_range_start_mv) % config->second_step_mv != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        encoded = first_codes +
                  (millivolts - config->second_range_start_mv) / config->second_step_mv;
    } else {
        if ((millivolts - config->minimum_mv) % config->step_mv != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        encoded = (millivolts - config->minimum_mv) / config->step_mv;
    }

    if (encoded > config->voltage_mask) {
        return ESP_ERR_INVALID_ARG;
    }
    *code = (uint8_t)encoded;
    return ESP_OK;
}

static uint16_t decode_voltage(const regulator_config_t *config, uint8_t code)
{
    uint16_t millivolts = 0;
    if (config->second_range_start_mv != 0) {
        const uint16_t first_codes =
            (config->second_range_start_mv - config->minimum_mv) / config->step_mv;
        if (code > first_codes) {
            millivolts = config->second_range_start_mv +
                         (code - first_codes) * config->second_step_mv;
            return millivolts < config->maximum_mv ? millivolts : config->maximum_mv;
        }
    }
    millivolts = config->minimum_mv + code * config->step_mv;
    return millivolts < config->maximum_mv ? millivolts : config->maximum_mv;
}

esp_err_t tg28_sw_encode_regulator_voltage(tg28_sw_regulator_t regulator,
        uint16_t millivolts, uint8_t *code)
{
    ESP_RETURN_ON_FALSE(regulator_is_valid(regulator) && code != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid voltage encode request");
    return encode_voltage(&s_regulators[regulator], millivolts, code);
}

uint16_t tg28_sw_decode_regulator_voltage(tg28_sw_regulator_t regulator,
        uint8_t code)
{
    if (!regulator_is_valid(regulator)) {
        return 0;
    }
    const regulator_config_t *config = &s_regulators[regulator];
    return decode_voltage(config, code & config->voltage_mask);
}

esp_err_t tg28_sw_create(i2c_master_bus_handle_t bus,
                         const tg28_sw_config_t *config,
                         tg28_sw_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && ret_handle != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid create arguments");
    *ret_handle = NULL;

    const tg28_sw_config_t default_config = TG28_SW_CONFIG_DEFAULT();
    const tg28_sw_config_t *device_config = config != NULL ? config : &default_config;
    ESP_RETURN_ON_FALSE(device_config->device_address <= 0x7F &&
                        device_config->scl_speed_hz > 0 &&
                        ((device_config->battery_model == NULL &&
                          device_config->battery_model_size == 0) ||
                         (device_config->battery_model != NULL &&
                          device_config->battery_model_size > 0)),
                        ESP_ERR_INVALID_ARG, TAG, "invalid I2C configuration");

    tg28_sw_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, TAG, "allocation failed");
    handle->lock = xSemaphoreCreateMutex();
    if (handle->lock == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    const i2c_device_config_t i2c_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_config->device_address,
        .scl_speed_hz = device_config->scl_speed_hz,
    };
    esp_err_t error = i2c_master_bus_add_device(bus, &i2c_config, &handle->i2c_device);
    if (error != ESP_OK) {
        vSemaphoreDelete(handle->lock);
        free(handle);
        return error;
    }

    uint8_t chip_id = 0;
    error = read_registers(handle, TG28_SW_REG_CHIP_ID, &chip_id, sizeof(chip_id));
    if (error != ESP_OK) {
        i2c_master_bus_rm_device(handle->i2c_device);
        vSemaphoreDelete(handle->lock);
        free(handle);
        return error;
    }
    if (!tg28_sw_is_supported_chip_id(chip_id)) {
        ESP_LOGW(TAG, "unexpected chip ID: 0x%02x", chip_id);
    }

    if (device_config->battery_model != NULL && device_config->battery_model_size > 0) {
        error = tg28_sw_program_battery_model(handle, device_config->battery_model,
                                               device_config->battery_model_size);
        if (error != ESP_OK) {
            i2c_master_bus_rm_device(handle->i2c_device);
            vSemaphoreDelete(handle->lock);
            free(handle);
            return error;
        }
    }

    *ret_handle = handle;
    return ESP_OK;
}

esp_err_t tg28_sw_delete(tg28_sw_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = i2c_master_bus_rm_device(handle->i2c_device);
    if (error != ESP_OK) {
        unlock_device(handle);
        return error;
    }
    handle->i2c_device = NULL;
    unlock_device(handle);
    vSemaphoreDelete(handle->lock);
    handle->lock = NULL;
    free(handle);
    return ESP_OK;
}

esp_err_t tg28_sw_get_chip_id(tg28_sw_handle_t handle, uint8_t *chip_id)
{
    ESP_RETURN_ON_FALSE(chip_id != NULL, ESP_ERR_INVALID_ARG, TAG, "chip ID is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = read_registers(handle, TG28_SW_REG_CHIP_ID,
                                           chip_id, sizeof(*chip_id));
    unlock_device(handle);
    return error;
}

bool tg28_sw_is_supported_chip_id(uint8_t chip_id)
{
    return chip_id == 0x47 || chip_id == 0x4A;
}

esp_err_t tg28_sw_get_status(tg28_sw_handle_t handle, tg28_sw_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");

    uint8_t raw[2] = {0};
    memset(status, 0, sizeof(*status));
    esp_err_t error = read_registers(handle, TG28_SW_REG_CHIP_ID,
                                     &status->chip_id, sizeof(status->chip_id));
    if (error == ESP_OK) {
        error = read_registers(handle, TG28_SW_REG_COMMON_STATUS0, raw, sizeof(raw));
    }
    if (error == ESP_OK) {
        status->common_status0 = raw[0];
        status->common_status1 = raw[1];
        status->vbus_present = (raw[0] & TG28_SW_VBUS_PRESENT_MASK) != 0;
        status->battery_present = (raw[0] & TG28_SW_BATTERY_PRESENT_MASK) != 0;
        const uint8_t charge_state = raw[1] & TG28_SW_CHARGE_STATE_MASK;
        status->charging = charge_state >= 1 && charge_state <= 3;
        status->charge_done = charge_state == TG28_SW_CHARGE_STATE_DONE;
        error = read_registers(handle, TG28_SW_REG_VBAT_H, raw, sizeof(raw));
    }
    if (error == ESP_OK) {
        const uint16_t adc = ((uint16_t)(raw[0] & 0x3F) << 8) | raw[1];
        /* The current vendor gauge driver exposes this 14-bit value in mV. */
        status->battery_mv = adc;
        error = read_registers(handle, TG28_SW_REG_SOC,
                               &status->battery_percent,
                               sizeof(status->battery_percent));
    }
    if (status->battery_percent > 100) {
        status->battery_percent = 100;
    }

    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_power_on_source(tg28_sw_handle_t handle, uint8_t *source)
{
    ESP_RETURN_ON_FALSE(source != NULL, ESP_ERR_INVALID_ARG, TAG, "source is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = read_registers(handle, TG28_SW_REG_POWER_ON_SOURCE,
                                           source, sizeof(*source));
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_configure_power_key_interrupts(tg28_sw_handle_t handle,
        uint8_t enabled_mask)
{
    ESP_RETURN_ON_FALSE((enabled_mask & ~TG28_SW_POWER_KEY_IRQ_MASK) == 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid power-key IRQ mask");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = update_bits(handle, TG28_SW_REG_INT_ENABLE1,
                                        TG28_SW_POWER_KEY_IRQ_MASK, enabled_mask);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_configure_external_fixed_ts(tg28_sw_handle_t handle)
{
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const uint8_t mask = TG28_SW_TS_EXTERNAL_FIXED_MASK |
                         TG28_SW_TS_CURRENT_ENABLE_MASK;
    const esp_err_t error = update_bits(handle, TG28_SW_REG_TS_CONFIG, mask,
                                        TG28_SW_TS_EXTERNAL_FIXED_MASK);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_set_charge_current(tg28_sw_handle_t handle, uint16_t milliamps)
{
    uint8_t code = 0;
    ESP_RETURN_ON_ERROR(tg28_sw_encode_charge_current(milliamps, &code),
                        TAG, "unsupported charge current");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = update_bits(handle, TG28_SW_REG_CHARGE_CURRENT,
                                        TG28_SW_CHARGE_CURRENT_MASK, code);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_charge_current(tg28_sw_handle_t handle, uint16_t *milliamps)
{
    ESP_RETURN_ON_FALSE(milliamps != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "charge current is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    uint8_t code = 0;
    esp_err_t error = read_registers(handle, TG28_SW_REG_CHARGE_CURRENT,
                                     &code, sizeof(code));
    if (error == ESP_OK) {
        code &= TG28_SW_CHARGE_CURRENT_MASK;
        if (code > 21) {
            ESP_LOGE(TAG, "reserved charge-current code: 0x%02x", code);
            error = ESP_ERR_INVALID_RESPONSE;
        } else {
            *milliamps = tg28_sw_decode_charge_current(code);
        }
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_set_input_current_limit(tg28_sw_handle_t handle,
        uint16_t milliamps)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    uint8_t code = 0;
    ESP_RETURN_ON_ERROR(tg28_sw_encode_input_current_limit(milliamps, &code),
                        TAG, "unsupported input current limit");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = update_bits(handle, TG28_SW_REG_INPUT_CURRENT_LIMIT,
                                        TG28_SW_INPUT_CURRENT_LIMIT_MASK, code);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_input_current_limit(tg28_sw_handle_t handle,
        uint16_t *milliamps)
{
    ESP_RETURN_ON_FALSE(handle != NULL && milliamps != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid limit request");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    uint8_t code = 0;
    esp_err_t error = read_registers(handle, TG28_SW_REG_INPUT_CURRENT_LIMIT,
                                     &code, sizeof(code));
    if (error == ESP_OK) {
        code &= TG28_SW_INPUT_CURRENT_LIMIT_MASK;
        if (code >= TG28_SW_INPUT_CURRENT_LIMIT_COUNT) {
            ESP_LOGE(TAG, "reserved input-current-limit code: 0x%02x", code);
            error = ESP_ERR_INVALID_RESPONSE;
        } else {
            *milliamps = tg28_sw_decode_input_current_limit(code);
        }
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_set_charge_voltage(tg28_sw_handle_t handle,
                                     uint16_t millivolts)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    uint8_t code = 0;
    ESP_RETURN_ON_ERROR(tg28_sw_encode_charge_voltage(millivolts, &code),
                        TAG, "unsupported charge voltage");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = update_bits(handle, TG28_SW_REG_CHARGE_VOLTAGE,
                                        TG28_SW_CHARGE_VOLTAGE_MASK, code);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_charge_voltage(tg28_sw_handle_t handle,
                                     uint16_t *millivolts)
{
    ESP_RETURN_ON_FALSE(handle != NULL && millivolts != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid voltage request");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    uint8_t code = 0;
    esp_err_t error = read_registers(handle, TG28_SW_REG_CHARGE_VOLTAGE,
                                     &code, sizeof(code));
    if (error == ESP_OK) {
        code &= TG28_SW_CHARGE_VOLTAGE_MASK;
        if (code >= TG28_SW_CHARGE_VOLTAGE_COUNT) {
            ESP_LOGE(TAG, "reserved charge-voltage code: 0x%02x", code);
            error = ESP_ERR_INVALID_RESPONSE;
        } else {
            *millivolts = tg28_sw_decode_charge_voltage(code);
        }
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_set_vindpm(tg28_sw_handle_t handle, uint16_t millivolts)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    uint8_t code = 0;
    ESP_RETURN_ON_ERROR(tg28_sw_encode_vindpm(millivolts, &code),
                        TAG, "unsupported VINDPM voltage");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const esp_err_t error = update_bits(handle, TG28_SW_REG_VINDPM,
                                        TG28_SW_VINDPM_MASK, code);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_vindpm(tg28_sw_handle_t handle, uint16_t *millivolts)
{
    ESP_RETURN_ON_FALSE(handle != NULL && millivolts != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid VINDPM request");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    uint8_t code = 0;
    esp_err_t error = read_registers(handle, TG28_SW_REG_VINDPM,
                                     &code, sizeof(code));
    if (error == ESP_OK) {
        *millivolts = tg28_sw_decode_vindpm(code);
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_program_battery_model(tg28_sw_handle_t handle,
                                        const uint8_t *model, size_t size)
{
    ESP_RETURN_ON_FALSE(model != NULL && size > 0, ESP_ERR_INVALID_ARG,
                        TAG, "battery model is empty");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");

    uint8_t module_enable = 0;
    esp_err_t error = read_registers(handle, TG28_SW_REG_MODULE_ENABLE,
                                     &module_enable, sizeof(module_enable));
    const bool module_state_valid = error == ESP_OK;
    bool brom_open = false;
    if (error == ESP_OK) {
        const uint8_t charger_off = module_enable & ~TG28_SW_CHARGER_ENABLE_MASK;
        error = write_registers(handle, TG28_SW_REG_MODULE_ENABLE,
                                &charger_off, sizeof(charger_off));
    }
    if (error == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(TG28_SW_CHARGER_SETTLE_MS));
        error = reset_gauge_mcu(handle);
    }
    if (error == ESP_OK) {
        error = set_brom_writer(handle, false);
    }
    if (error == ESP_OK) {
        error = set_brom_writer(handle, true);
        brom_open = error == ESP_OK;
    }
    for (size_t i = 0; error == ESP_OK && i < size; ++i) {
        error = write_registers(handle, TG28_SW_REG_BATTERY_MODEL,
                                &model[i], sizeof(model[i]));
    }

    if (error == ESP_OK) {
        error = set_brom_writer(handle, false);
        brom_open = error != ESP_OK;
    }
    if (error == ESP_OK) {
        error = set_brom_writer(handle, true);
        brom_open = error == ESP_OK;
    }
    for (size_t i = 0; error == ESP_OK && i < size; ++i) {
        uint8_t value = 0;
        error = read_registers(handle, TG28_SW_REG_BATTERY_MODEL,
                               &value, sizeof(value));
        if (error == ESP_OK && value != model[i]) {
            ESP_LOGE(TAG, "battery model verification failed at byte %u",
                     (unsigned)i);
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }

    esp_err_t cleanup_error = ESP_OK;
    if (brom_open) {
        cleanup_error = set_brom_writer(handle, false);
    }
    if (error == ESP_OK && cleanup_error == ESP_OK) {
        cleanup_error = update_bits(handle, TG28_SW_REG_FUEL_GAUGE_CONTROL,
                                    TG28_SW_BROM_UPDATE_MARK_MASK,
                                    TG28_SW_BROM_UPDATE_MARK_MASK);
    }
    /* Always reset the gauge MCU before restoring the charger, including on
     * a failed download or verification, so it cannot remain in BROM state. */
    const esp_err_t gauge_reset_error = reset_gauge_mcu(handle);
    if (cleanup_error == ESP_OK) {
        cleanup_error = gauge_reset_error;
    }
    const esp_err_t restore_error = module_state_valid ?
                                    write_registers(handle,
                                            TG28_SW_REG_MODULE_ENABLE,
                                            &module_enable,
                                            sizeof(module_enable)) : ESP_OK;
    unlock_device(handle);
    if (error != ESP_OK) {
        return error;
    }
    if (cleanup_error != ESP_OK) {
        return cleanup_error;
    }
    return restore_error;
}

esp_err_t tg28_sw_regulator_set_voltage(tg28_sw_handle_t handle,
                                        tg28_sw_regulator_t regulator,
                                        uint16_t millivolts)
{
    ESP_RETURN_ON_FALSE(regulator_is_valid(regulator), ESP_ERR_INVALID_ARG,
                        TAG, "invalid regulator");
    uint8_t code = 0;
    ESP_RETURN_ON_ERROR(tg28_sw_encode_regulator_voltage(regulator, millivolts,
                        &code),
                        TAG, "unsupported voltage");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const regulator_config_t *config = &s_regulators[regulator];
    const esp_err_t error = update_bits(handle, config->voltage_register,
                                        config->voltage_mask, code);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_regulator_get_voltage(tg28_sw_handle_t handle,
                                        tg28_sw_regulator_t regulator,
                                        uint16_t *millivolts)
{
    ESP_RETURN_ON_FALSE(regulator_is_valid(regulator) && millivolts != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid voltage request");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const regulator_config_t *config = &s_regulators[regulator];
    uint8_t code = 0;
    const esp_err_t error = read_registers(handle, config->voltage_register,
                                           &code, sizeof(code));
    if (error == ESP_OK) {
        *millivolts = tg28_sw_decode_regulator_voltage(regulator, code);
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_regulator_enable(tg28_sw_handle_t handle,
                                   tg28_sw_regulator_t regulator,
                                   bool enable)
{
    ESP_RETURN_ON_FALSE(regulator_is_valid(regulator), ESP_ERR_INVALID_ARG,
                        TAG, "invalid regulator");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const regulator_config_t *config = &s_regulators[regulator];
    const esp_err_t error = update_bits(handle, config->enable_register,
                                        config->enable_mask,
                                        enable ? config->enable_mask : 0);
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_regulator_is_enabled(tg28_sw_handle_t handle,
                                       tg28_sw_regulator_t regulator,
                                       bool *enabled)
{
    ESP_RETURN_ON_FALSE(regulator_is_valid(regulator) && enabled != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid state request");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    const regulator_config_t *config = &s_regulators[regulator];
    uint8_t value = 0;
    const esp_err_t error = read_registers(handle, config->enable_register,
                                           &value, sizeof(value));
    if (error == ESP_OK) {
        *enabled = (value & config->enable_mask) != 0;
    }
    unlock_device(handle);
    return error;
}

esp_err_t tg28_sw_get_and_clear_interrupts(tg28_sw_handle_t handle,
        uint8_t status[3])
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");
    ESP_RETURN_ON_ERROR(lock_device(handle), TAG, "device lock failed");
    esp_err_t error = read_registers(handle, TG28_SW_REG_INT_STATUS1, status, 3);
    if (error == ESP_OK) {
        error = write_registers(handle, TG28_SW_REG_INT_STATUS1, status, 3);
    }
    unlock_device(handle);
    return error;
}

const char *tg28_sw_regulator_name(tg28_sw_regulator_t regulator)
{
    return regulator_is_valid(regulator) ? s_regulators[regulator].name : "invalid";
}
