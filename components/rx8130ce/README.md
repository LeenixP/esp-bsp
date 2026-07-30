# RX8130CE RTC driver

This component provides an ESP-IDF C driver for the Epson RX8130CE real-time
clock. It uses an existing `i2c_master` bus and does not own the bus, interrupt
GPIO, backup supply, or board power policy.

The current API covers the functions needed by board bring-up:

- create and remove an RX8130CE device on an existing I2C bus;
- initialize every user register after a voltage-loss (`VLF`) event;
- validate, read, and set calendar time from 2000 through 2099;
- decode retained voltage, reset, alarm, timer, and update flags;
- read and clear the three interrupt flags that can assert `/IRQ`.

Alarm programming, wake-up timer configuration, clock output, and digital offset
are intentionally outside the initial API. Those
features need application-specific choices and should be added with tests when
a board uses them.

The board uses a primary backup cell. Device creation therefore always clears
`CHGEN` and sets `INIEN`: automatic supply switchover is enabled, while backup
battery charging is kept off. After `VLF=1`, the driver waits for oscillator
startup and initializes all documented user registers. The calendar starts at
the explicit recovery epoch 2000-01-01 00:00:00 (Saturday), and the application
should set real time before relying on timestamps.

## Basic use

```c
#include "driver/i2c_master.h"
#include "rx8130ce.h"

rx8130ce_handle_t rtc = NULL;
rx8130ce_config_t config = RX8130CE_CONFIG_DEFAULT();

ESP_ERROR_CHECK(rx8130ce_create(i2c_bus, &config, &rtc));

rx8130ce_time_t time;
rx8130ce_status_t status;
ESP_ERROR_CHECK(rx8130ce_get_time(rtc, &time, &status));

ESP_ERROR_CHECK(rx8130ce_delete(rtc));
```

The fixed 7-bit address is `0x32`. `RX8130CE_CONFIG_DEFAULT()` selects a
400 kHz I2C clock.

## Setting time

`rx8130ce_set_time()` validates the complete calendar value, sets the device
`STOP` bit, writes all seven calendar registers in one transaction, clears the
voltage-loss flag, and restores the original control register. Weekday uses
`0` for Sunday through `6` for Saturday.

The RTC application manual warns that voltage detection and supply switching
are affected while `STOP` is set. The driver therefore keeps that interval to
the calendar write and always attempts to restore the original control value
if an intermediate transaction fails.

## Status and interrupts

`status.time_valid` is false while the `VLF` flag is set. A successful I2C read
does not by itself prove that retained time is valid.

`rx8130ce_get_and_clear_interrupts()` returns the complete flag register and
clears only `UF`, `TF`, and `AF`. Voltage-loss, backup, and reset evidence is
left unchanged. GPIO interrupt registration belongs to the board or
application because the RX8130CE `/IRQ` output may be shared with another
device.

## References

- [RX8130CE product page](https://www.epsondevice.com/crystal/en/products/rtc/rx8130ce.html)
- [RX8130CE application manual](https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf)

No datasheet is copied into the component archive.
