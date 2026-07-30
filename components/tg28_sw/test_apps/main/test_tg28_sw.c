/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>

#include "tg28_sw.h"

void app_main(void)
{
    assert(tg28_sw_is_supported_chip_id(0x47));
    assert(tg28_sw_is_supported_chip_id(0x4A));
    assert(!tg28_sw_is_supported_chip_id(0x00));
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_DCDC1), "dcdc1") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_BLDO2), "bldo2") == 0);
    assert(strcmp(tg28_sw_regulator_name(TG28_SW_REGULATOR_COUNT), "invalid") == 0);
}
