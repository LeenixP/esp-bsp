/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>

#include "tg28_sw.h"

/* Pure conversion helpers from tg28_sw.c, exercised here without I2C
 * hardware. They are deliberately not part of the public header. */
esp_err_t tg28_sw_encode_charge_current(uint16_t milliamps, uint8_t *code);
uint16_t tg28_sw_decode_charge_current(uint8_t code);
esp_err_t tg28_sw_encode_input_current_limit(uint16_t milliamps, uint8_t *code);
uint16_t tg28_sw_decode_input_current_limit(uint8_t code);
esp_err_t tg28_sw_encode_charge_voltage(uint16_t millivolts, uint8_t *code);
uint16_t tg28_sw_decode_charge_voltage(uint8_t code);
esp_err_t tg28_sw_encode_vindpm(uint16_t millivolts, uint8_t *code);
uint16_t tg28_sw_decode_vindpm(uint8_t code);
esp_err_t tg28_sw_encode_regulator_voltage(tg28_sw_regulator_t regulator,
        uint16_t millivolts, uint8_t *code);
uint16_t tg28_sw_decode_regulator_voltage(tg28_sw_regulator_t regulator,
        uint8_t code);

static void test_chip_id_and_names(void)
{
    assert(tg28_sw_is_supported_chip_id(0x47));
    assert(tg28_sw_is_supported_chip_id(0x4A));
    assert(!tg28_sw_is_supported_chip_id(0x00));

    assert(TG28_SW_REGULATOR_COUNT == 12);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_DCDC1), "dcdc1") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_ALDO4), "aldo4") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_BLDO2), "bldo2") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_DLDO1), "dldo1") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_DLDO2), "dldo2") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_REGULATOR_COUNT), "invalid") == 0);
}

static void test_charge_current_coding(void)
{
    uint8_t code = 0;
    assert(tg28_sw_encode_charge_current(0, &code) == ESP_OK && code == 0);
    assert(tg28_sw_encode_charge_current(25, &code) == ESP_OK && code == 1);
    assert(tg28_sw_encode_charge_current(200, &code) == ESP_OK && code == 8);
    assert(tg28_sw_encode_charge_current(300, &code) == ESP_OK && code == 9);
    assert(tg28_sw_encode_charge_current(1500, &code) == ESP_OK && code == 21);
    assert(tg28_sw_encode_charge_current(225, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_charge_current(250, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_charge_current(1600, &code) == ESP_ERR_INVALID_ARG);

    assert(tg28_sw_decode_charge_current(0) == 0);
    assert(tg28_sw_decode_charge_current(8) == 200);
    assert(tg28_sw_decode_charge_current(9) == 300);
    assert(tg28_sw_decode_charge_current(21) == 1500);
}

static void test_input_current_limit_coding(void)
{
    static const uint16_t levels[] = {100, 500, 900, 1000, 1500, 2000};
    for (uint8_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        uint8_t code = 0;
        assert(tg28_sw_encode_input_current_limit(levels[i], &code) == ESP_OK);
        assert(code == i);
        assert(tg28_sw_decode_input_current_limit(code) == levels[i]);
    }

    uint8_t code = 0;
    assert(tg28_sw_encode_input_current_limit(2500, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_input_current_limit(0, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_decode_input_current_limit(6) == 0);
    assert(tg28_sw_decode_input_current_limit(7) == 0);
}

static void test_charge_voltage_coding(void)
{
    static const uint16_t levels[] = {3900, 4000, 4100, 4200, 4350, 4400};
    for (uint8_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        uint8_t code = 0;
        assert(tg28_sw_encode_charge_voltage(levels[i], &code) == ESP_OK);
        assert(code == i);
        assert(tg28_sw_decode_charge_voltage(code) == levels[i]);
    }

    uint8_t code = 0;
    assert(tg28_sw_encode_charge_voltage(4300, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_charge_voltage(4500, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_charge_voltage(3800, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_decode_charge_voltage(6) == 0);
    assert(tg28_sw_decode_charge_voltage(7) == 0);
}

static void test_vindpm_coding(void)
{
    uint8_t code = 0;
    assert(tg28_sw_encode_vindpm(4040, &code) == ESP_OK && code == 2);
    assert(tg28_sw_encode_vindpm(4680, &code) == ESP_OK && code == 10);
    assert(tg28_sw_encode_vindpm(4000, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_vindpm(4700, &code) == ESP_ERR_INVALID_ARG);
    assert(tg28_sw_encode_vindpm(3960, &code) == ESP_ERR_INVALID_ARG);

    assert(tg28_sw_decode_vindpm(2) == 4040);
    assert(tg28_sw_decode_vindpm(10) == 4680);
}

static void test_regulator_voltage_coding(void)
{
    uint8_t code = 0;

    /* ALDO4: the whole 500-3500mV range must be reachable, including
     * 3500mV (code 30), which the vendor two-segment table cannot encode. */
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_ALDO4, 500, &code) == ESP_OK &&
           code == 0);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_ALDO4, 3400, &code) == ESP_OK &&
           code == 29);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_ALDO4, 3500, &code) == ESP_OK &&
           code == 30);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_ALDO4, 3550, &code) ==
           ESP_ERR_INVALID_ARG);
    assert(tg28_sw_decode_regulator_voltage(TG28_SW_ALDO4, 30) == 3500);
    assert(tg28_sw_decode_regulator_voltage(TG28_SW_ALDO4, 31) == 3500);

    /* DLDO1: 500-3500mV in 100mV steps, 5-bit code. */
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DLDO1, 3300, &code) == ESP_OK &&
           code == 28);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DLDO1, 3350, &code) ==
           ESP_ERR_INVALID_ARG);
    assert(tg28_sw_decode_regulator_voltage(TG28_SW_DLDO1, 28) == 3300);

    /* DLDO2: 500-1400mV in 50mV steps. */
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DLDO2, 500, &code) == ESP_OK &&
           code == 0);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DLDO2, 1400, &code) == ESP_OK &&
           code == 18);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DLDO2, 1450, &code) ==
           ESP_ERR_INVALID_ARG);
    assert(tg28_sw_decode_regulator_voltage(TG28_SW_DLDO2, 18) == 1400);

    /* Existing rails keep their coding: BLDO1 full range, DCDC2 two segments. */
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_BLDO1, 3500, &code) == ESP_OK &&
           code == 30);
    assert(tg28_sw_encode_regulator_voltage(TG28_SW_DCDC2, 1200, &code) == ESP_OK);
    assert(tg28_sw_decode_regulator_voltage(TG28_SW_DCDC2,
                                            70) == 1200);
}

void app_main(void)
{
    test_chip_id_and_names();
    test_charge_current_coding();
    test_input_current_limit_coding();
    test_charge_voltage_coding();
    test_vindpm_coding();
    test_regulator_voltage_coding();
}
