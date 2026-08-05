# ChangeLog

## Unreleased

### Features

* Add explicit power-management API: `esp_lcd_touch_cst820_sleep()` / `esp_lcd_touch_cst820_wakeup()` for the deep-sleep tier (~2 uA, wake by reset only) and `esp_lcd_touch_cst820_enter_monitor_mode()` / `esp_lcd_touch_cst820_exit_monitor_mode()` for the standby tier (~10 uA) in which a touch pulses INT and wakes the host. The framework `enter_sleep`/`exit_sleep` hooks are unchanged and route to the same code paths
* Test apps: add a hardware-free NULL-handle argument check, a deep-sleep round-trip case and a monitor-mode light-sleep touch wake-up case

### Documentation

* Record the provenance of the sleep command register assumption (0xA5 <- 0x03 from public CST816-family sources, conflicting 0xE5 claim from DriveBus) and mark it pending EVT verification in the source, header and README
* Document the datasheet standby-mode behavior (auto-standby 2 s after the last touch, INT wake of the host) and a Candis-S31 low-power sequence

### Bug Fixes

* Test app: correct the hardcoded Candis-S31 wiring — SDA=GPIO34, SCL=GPIO33 (main I2C bus), RST=GPIO17, INT=GPIO3. The previous values drove GPIO7, which is the low-power bus SDA shared by the PMIC and RTC

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
