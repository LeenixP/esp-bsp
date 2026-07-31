# ChangeLog

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
