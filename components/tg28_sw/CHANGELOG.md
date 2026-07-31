# ChangeLog

## v0.2.0 - 2026-07-31

### Features

* Add DLDO1/DLDO2 to the regulator enum in place of DCDC5
* Add `tg28_sw_set_input_current_limit`/`tg28_sw_get_input_current_limit` (REG16, discrete vendor levels 100-2000 mA)
* Add `tg28_sw_set_charge_voltage`/`tg28_sw_get_charge_voltage` (REG64, discrete vendor levels 3900-4400 mV)
* Add `tg28_sw_set_vindpm`/`tg28_sw_get_vindpm` (REG15, 4040-4680 mV in 80 mV steps)
