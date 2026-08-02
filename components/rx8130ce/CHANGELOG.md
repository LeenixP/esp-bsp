# ChangeLog

## Unreleased

### Bug Fixes

* Add a per-device mutex so concurrent calls from application tasks and the shared interrupt service cannot interleave the read-modify-write sequences of `set_time`/`set_alarm`/`set_timer` and the IRQ helpers, matching the concurrency model of the tg28_sw driver

## v0.3.0 - 2026-07-31

### Features

* Make the backup battery charge policy configurable: new `rx8130ce_config_t.backup_charge_enable` field (default `false`, keeping the previous primary-cell behavior); `INIEN` is always set and `CHGEN` now follows the configured policy in both the normal power-up path and the `VLF=1` full-register initialization
* Accept `NULL` as the config of `rx8130ce_create`, falling back to `RX8130CE_CONFIG_DEFAULT()`
* Add fixed-cycle wake-up timer support: `rx8130ce_timer_t`, `rx8130ce_timer_source_t`, `rx8130ce_timer_is_valid`, `rx8130ce_timer_encode`, `rx8130ce_set_timer`, `rx8130ce_get_timer`, and `rx8130ce_timer_irq_enable` (`TIE`)
* Add `rx8130ce_update_irq_enable` for the time update interrupt (`UIE`)

### Fixes

* Declare the missing `freertos` private dependency in CMakeLists.txt

## v0.2.0 - 2026-07-31

### Features

* Add alarm support: `rx8130ce_alarm_t`, `rx8130ce_set_alarm`, `rx8130ce_get_alarm`, and `rx8130ce_alarm_irq_enable`
