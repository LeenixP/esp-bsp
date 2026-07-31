# ChangeLog

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
