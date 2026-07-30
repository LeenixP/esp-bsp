/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "fusb303b.h"

void app_main(void)
{
    assert(fusb303b_is_supported_identity(0x10, 0x03));
    assert(fusb303b_is_supported_identity(0x1F, 0x03));
    assert(!fusb303b_is_supported_identity(0x20, 0x03));
    assert(!fusb303b_is_supported_identity(0x10, 0x00));
}
