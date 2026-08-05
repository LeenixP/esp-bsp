# ChangeLog

## Unreleased

### Features

* Add load-switch channels `TG28_SW_SWITCH_DC1SW`/`TG28_SW_SWITCH_DC4SW` with `tg28_sw_switch_enable`/`tg28_sw_switch_is_enabled`/`tg28_sw_switch_name` (REG90 bit7 / REG91 bit0, the same physical bits as the DLDO1/DLDO2 LDO enables)
* Add 23 named interrupt sources `tg28_sw_irq_t` covering REG40-REG42 / REG48-REG4A and per-bit `tg28_sw_set_irq_enable_bit`/`tg28_sw_get_irq_enable_bit`; slot 21 (REG42 bit5) is reserved and has no symbol
* Add `tg28_sw_set_precharge_current`/`tg28_sw_get_precharge_current` (REG61, 0-200 mA in 25 mA steps)
* Add `tg28_sw_set_termination_current`/`tg28_sw_get_termination_current` (REG63, 0-200 mA in 25 mA steps plus the termination-enable bit)
* Add `tg28_sw_set_low_battery_warning`/`tg28_sw_get_low_battery_warning` (REG1A, level1 0-15 % and level2 5-20 % in 1 % steps)
* Widen the REG15 VINDPM window to the full hardware range 3880-5080 mV (codes 0-15)
* Add hardware-gated test app `test_tg28_sw_hw.c` (`CONFIG_TG28_SW_TEST_WITH_HARDWARE`)

### Fixes

* Correct the REG64 charge-voltage table for the switch-charger variant: code 0 is reserved, so the valid levels are 4000/4100/4200/4350/4400 mV and the linear-variant 3900 mV level is dropped
* Cap DLDO1 at 3300 mV per the REG99 enumeration (codes 29-31 reserved); the "0.5-3.4V" headline is contradicted by the enumeration
* Mask VINDPM to bits 3:0 so the read-only REG15 bits 7:4 are never written or fed into the decode

### Breaking changes

* `tg28_sw_set_charge_voltage` no longer accepts 3900 mV (reserved code on the switch-charger variant)
* `tg28_sw_regulator_set_voltage(TG28_SW_DLDO1, ...)` rejects values above 3300 mV

## v0.3.0 - 2026-07-31

### Features

* Add CPUSLDO to the regulator enum (REG98, 500-1400 mV in 50 mV steps, enable REG90 bit6)
* Add `tg28_sw_set_ts_config` (REG50 TS mode, current-source switch, and 20/40/50/60 uA current selection)
* Add `tg28_sw_set_irq_enable`/`tg28_sw_get_irq_enable` covering the REG40-REG42 IRQ enable banks
* Add `tg28_sw_read_adc_channel` and `tg28_sw_set_adc_channel_enable`/`tg28_sw_get_adc_channel_enable` for the VBAT/TS/VBUS/VSYS/TDIE ADC channels (REG34-REG3D, REG30)
* Move the pure conversion helper declarations into `priv_include/tg28_sw_priv.h`

### Breaking changes

* Remove `tg28_sw_configure_external_fixed_ts`; use `tg28_sw_set_ts_config` instead
* Rename the internal interrupt register macros to the datasheet numbering (`TG28_SW_REG_INT_ENABLE0/1/2`, `TG28_SW_REG_INT_STATUS0`)

## v0.2.0 - 2026-07-31

### Features

* Add DLDO1/DLDO2 to the regulator enum in place of DCDC5
* Add `tg28_sw_set_input_current_limit`/`tg28_sw_get_input_current_limit` (REG16, discrete vendor levels 100-2000 mA)
* Add `tg28_sw_set_charge_voltage`/`tg28_sw_get_charge_voltage` (REG64, discrete vendor levels 3900-4400 mV)
* Add `tg28_sw_set_vindpm`/`tg28_sw_get_vindpm` (REG15, 4040-4680 mV in 80 mV steps)
