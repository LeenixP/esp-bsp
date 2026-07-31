# ChangeLog

## v1.1.1 - 2026-07-31

### Bug Fixes

* Clamp the `get_track_id` loop to `CONFIG_ESP_LCD_TOUCH_MAX_POINTS` so a larger caller-supplied array size cannot read past the internal coordinate buffer

### Documentation

* Remove the Component Registry badge from the README until the component is published
* Document the test-board wiring behind the hardcoded test app pins and clarify the H_RES/V_RES defines

## v1.1.0 - 2026-07-31

### Features

* Add deep-sleep support: `enter_sleep`/`exit_sleep` through the 0xA5 sleep-mode register, with wake-up via a hardware reset cycle

## v1.0.0

* Initial release: CST820 capacitive touch controller driver for `esp_lcd_touch`
