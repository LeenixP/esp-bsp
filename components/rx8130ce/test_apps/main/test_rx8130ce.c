/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "rx8130ce.h"

void app_main(void)
{
    const rx8130ce_time_t leap_day = {
        .year = 2028,
        .month = 2,
        .day = 29,
        .weekday = 2,
        .hour = 23,
        .minute = 59,
        .second = 59,
    };
    assert(rx8130ce_time_is_valid(&leap_day));

    rx8130ce_time_t invalid = leap_day;
    invalid.year = 2027;
    assert(!rx8130ce_time_is_valid(&invalid));
    invalid = leap_day;
    invalid.weekday = 7;
    assert(!rx8130ce_time_is_valid(&invalid));
    invalid = leap_day;
    invalid.second = 60;
    assert(!rx8130ce_time_is_valid(&invalid));
    assert(!rx8130ce_time_is_valid(NULL));
}
