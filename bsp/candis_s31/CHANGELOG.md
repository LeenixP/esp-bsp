# ChangeLog

## Unreleased

### Features

* PMIC: add `bsp_pmic_read_adc_mv()` with the `bsp_pmic_adc_channel_t` channel list (VBAT/TS/VBUS/VSYS/TDIE); channels disabled at the OTP level are enabled for the measurement and restored
* PMIC: add the load-switch API `bsp_pmic_switch_enable()`/`bsp_pmic_switch_is_enabled()`/`bsp_pmic_switch_name()` with the `bsp_pmic_switch_t` list (`BSP_PMIC_SWITCH_DC1SW`/`BSP_PMIC_SWITCH_DC4SW`), wrapping the tg28_sw switch channels

### Fixed

* Audio: `bsp_audio_init()` built its default `i2s_std_config_t` from a hard-coded 22050 Hz and ignored `BSP_I2S_SAMPLE_RATE` entirely, so overriding the macro had no effect; it now derives the rate from the macro. The default itself moves from 22050 Hz to 16000 Hz: 22050 Hz has no row in the es8389 coefficient table (`coeff_div[]` in esp_codec_dev `~1.5` holds only 8000/16000/24000/32000/44100/48000/88200/96000/192000 Hz). This BSP passes `use_mclk = true`, which makes `es8389_set_fs()` skip that lookup (and it discards the return value anyway), so 22050 Hz did not fail outright - it simply left the codec clock dividers at their reset defaults. Keeping the default inside the table makes both the MCLK-clocked and BCLK-clocked paths well defined
* Power: DLDO1 is modelled as the DC1SW load switch its OTP straps it to instead of as an adjustable LDO. The RGB LED rail is opened with `bsp_pmic_switch_enable(BSP_PMIC_SWITCH_DC1SW, true)` in `bsp_led_indicator_create()` and closed again in `bsp_power_safe_state()`; no voltage is programmed on DLDO1 any more
* Camera: XCLK is no longer driven twice. The CAM controller derives the 24 MHz sensor clock itself and drives it on `dvp_pin.xclk_io`, so the redundant LEDC channel is now behind `CONFIG_BSP_CAMERA_XCLK_USE_LEDC`, disabled by default (diagnostics only); `CONFIG_BSP_CAMERA_XCLK_LEDC_CH` only applies when it is enabled
* Interrupts: the shared PMIC/RTC line on GPIO2 is now level-triggered (`GPIO_INTR_LOW_LEVEL`) instead of falling-edge; an event asserted while the other device still holds the line low no longer goes unnoticed. The ISR masks the line and `bsp_shared_irq_service()` re-arms it after draining both devices. Re-registering a callback now replaces the previous handler instead of failing

## v1.2.0 - 2026-07-31

### Features

* PMIC: add the `BSP_PMIC_CPUSLDO` rail enum, following the tg28_sw 0.3.0 rail table (unconnected on this board, kept off); rail count is now thirteen
* RTC: pass `backup_charge_enable = false` explicitly at driver creation (rx8130ce 0.3.0 config field); Candis-S31 uses a primary backup cell
* Power: select the board's TS input through the generalized `tg28_sw_set_ts_config` API (external fixed TS, current source off)

### Notes

* Driver dependencies raised to tg28_sw `^0.3.0`, rx8130ce `^0.3.0`, fusb303b `^0.2.0`

## v1.1.0 - 2026-07-31

### Features

* Display: configure the CO5300 TE input (GPIO16 via R55) and fix the QSPI write command encoding; `BSP_LCD_BIGENDIAN` is now 1
* Display: panel sleep and deep-standby entry/exit through the CO5300 command path
* Power: display VBAT/VCI rail sequencing (ALDO1-sourced VCI, 2 ms staging, reverse order on power-down) and pin tristating for the camera and SD domains
* PMIC: replace the `BSP_PMIC_DCDC5` rail with `BSP_PMIC_DLDO1`/`BSP_PMIC_DLDO2`; expose input current limit, charge voltage, and VINDPM settings
* RTC: alarm support via `bsp_rtc_set_alarm`/`bsp_rtc_get_alarm`/`bsp_rtc_alarm_irq_enable` (`bsp_rtc_alarm_t`)
* Interrupts: shared PMIC/RTC IRQ line service with GPIO ISR dispatch and `bsp_shared_irq_register_callback`
* Camera: OV5640 DVP XCLK changed to 24 MHz; drop unused flip/rotation macros

### Notes

* Build baseline: ESP-IDF `v6.1-beta1`
