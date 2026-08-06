# BSP: Candis-S31

| [Hardware repository](https://github.com/jlckfb/Candis-S31) | [API](API.md) | [Examples](#compatible-bsp-examples) | [![Component Registry](https://components.espressif.com/components/espressif/candis_s31/badge.svg)](https://components.espressif.com/components/espressif/candis_s31) | ![maintenance-status](https://img.shields.io/badge/maintenance-actively--developed-brightgreen.svg) |
| --- | --- | --- | --- | --- |

## Overview

Candis-S31 is a compact ESP32-S31 board built around a 2.0-inch 460 × 460
AMOLED. The board also carries capacitive touch, battery charging and power
management, an RTC, two USB Type-C connectors, an ES8389 audio codec, a DVP
camera connector, a microSD slot, and one addressable RGB LED.

This BSP follows schematic revision 0.5. The implementation and examples are
compile-tested with ESP-IDF 6.1, but the first hardware revision has not been
fabricated yet. GPIO polarity, rail voltage, timing, and peripheral behavior
must be confirmed during EVT bring-up before the component is released.

## Capabilities and dependencies

<div align="center">
<!-- START_DEPENDENCIES -->

|     Available    |       Capability       |Controller/Codec|                                                  Component                                                 |   Version  |
|------------------|------------------------|----------------|------------------------------------------------------------------------------------------------------------|------------|
|:heavy_check_mark:|     :pager: DISPLAY    |     co5300     |                                                     idf                                                    |    >=6.1   |
|:heavy_check_mark:|:black_circle: LVGL_PORT|                |       [espressif/esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port)       |     ^2     |
|:heavy_check_mark:|    :point_up: TOUCH    |     cst820     |[espressif/esp_lcd_touch_cst820](https://components.espressif.com/components/espressif/esp_lcd_touch_cst820)|   ^1.0.0   |
|        :x:       | :radio_button: BUTTONS |                |                                                                                                            |            |
|        :x:       |   :white_circle: KNOB  |                |                                                                                                            |            |
|:heavy_check_mark:|  :musical_note: AUDIO  |                |       [espressif/esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev)       |    ~1.5    |
|:heavy_check_mark:| :speaker: AUDIO_SPEAKER|     es8389     |                                                                                                            |            |
|:heavy_check_mark:| :microphone: AUDIO_MIC |     es8389     |                                                                                                            |            |
|:heavy_check_mark:|  :floppy_disk: SDCARD  |                |                                                     idf                                                    |    >=6.1   |
|:heavy_check_mark:|       :bulb: LED       |                |   idf<br/>[espressif/led_indicator](https://components.espressif.com/components/espressif/led_indicator)   |>=6.1<br/>^2|
|:heavy_check_mark:|     :camera: CAMERA    |     OV5640     |           [espressif/esp_video](https://components.espressif.com/components/espressif/esp_video)           |    ~2.2    |
|:heavy_check_mark:|      :battery: BAT     |                |                                                     idf                                                    |    >=6.1   |
|        :x:       |    :video_game: IMU    |                |                                                                                                            |            |
|        :x:       | :thermometer: HUMITURE |                |                                                                                                            |            |

<!-- END_DEPENDENCIES -->
</div>

The TG28_SW driver is a reusable device component. Board-specific rail names,
voltage choices, enable ordering, and shutdown behavior stay in this BSP.
RX8130CE RTC and FUSB303B Type-C support are implemented locally because no
matching registry components are currently used by this board.

The board uses the switch-charger variant of the TG28 (I2C address 0x34 on
the low-power bus). It exposes thirteen rails through `bsp_pmic_regulator_*`:
DCDC1-DCDC4, ALDO1-ALDO4, BLDO1-BLDO2, CPUSLDO, and DLDO1-DLDO2. The
linear-charger variant's DCDC5 does not exist here. CPUSLDO is unconnected on
this board and stays off.

The DLDO1 pin is not an LDO on this board: the TG28 OTP (confirmation sheet
V1.3) straps it as the DC1SW load switch, so its voltage register is inert and
the output passes DCDC1 (3.3V) straight through to the WS2812B RGB LED. The
rail is OFF after power-on and software must open it explicitly, which is what
`bsp_pmic_switch_enable(BSP_PMIC_SWITCH_DC1SW, true)` does; the
`bsp_pmic_regulator_*` calls must not be used for it. `bsp_led_indicator_create()`
opens the switch and `bsp_power_safe_state()` closes it again. DLDO2 (DC4SW) is
an unconnected spare.
Charger control covers the constant-current limit (0-200mA in 25mA steps,
then 300-1500mA in 100mA steps), the discrete input current limit
(100/500/900/1000/1500/2000mA), and the discrete charge termination voltage
(3900/4000/4100/4200/4350/4400mV). `bsp_pmic_init()` clears any latched
interrupt status before enabling the power-key interrupts.

## Third-party notices

- The CO5300 initialization sequence in `bsp_display.c` is converted from the
  AM200Q460460LK module supplier's reference material. Its license status is
  being confirmed with the supplier; treat the sequence as supplier-provided
  reference data until that confirmation is complete.
- Touch support resolves `espressif/esp_lcd_touch_cst820` to the in-tree copy
  under `components/lcd_touch/esp_lcd_touch_cst820` (Apache-2.0) through
  `override_path` instead of pulling a registry package. The in-tree component
  is a self-maintained implementation, independent of the same-named
  `kodediy/esp_lcd_touch_cst820` registry package that earlier revisions of
  this BSP referenced; it keeps the module-specific CST820 report handling
  maintainable. It remains the Espressif `esp_lcd_touch` driver and is
  functionally equivalent.

## Compatible BSP examples

<div align="center">
<!-- START_EXAMPLES -->

| Example | Description | Try with ESP Launchpad |
| ------- | ----------- | ---------------------- |
| [Display Example](https://github.com/espressif/esp-bsp/tree/master/examples/display) | Show an image on the screen with a simple startup animation (LVGL) | [Flash Example](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display-) |
| [Camera Example](https://github.com/espressif/esp-bsp/tree/master/examples/display_camera_video) | Stream camera output to display (LVGL) | [Flash Example](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display_camera_video) |
| [LVGL Benchmark Example](https://github.com/espressif/esp-bsp/tree/master/examples/display_lvgl_benchmark) | Run LVGL benchmark tests | - |
| [LVGL Demos Example](https://github.com/espressif/esp-bsp/tree/master/examples/display_lvgl_demos) | Run the LVGL demo player - all LVGL examples are included (LVGL) | [Flash Example](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display_lvgl_demos-) |
| [Display SD card Example](https://github.com/espressif/esp-bsp/tree/master/examples/display_sdcard) | Example of mounting an SD card using SD-MMC/SPI with display interaction. This example is also supported on boards without a display. | [Flash Example](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display_sdcard) |
| [USB HID Example](https://github.com/espressif/esp-bsp/tree/master/examples/display_usb_hid) | USB HID demo (keyboard, mouse, or gamepad visualization using LVGL) | - |

<!-- END_EXAMPLES -->
</div>

## Using the BSP before release

For local bring-up, add `bsp/candis_s31` to `EXTRA_COMPONENT_DIRS` and require
the `candis_s31` component from the application. The common include is:

```c
#include "bsp/esp-bsp.h"
```

Call `bsp_board_init()` first. It sets direct enables and optional rails to a
disabled state; peripherals are initialized only when their individual BSP API
is called. See [API.md](API.md) for resource ownership and shutdown rules.

The reset, power-on, and boot keys are dedicated to the reset path, PMIC, and
boot strapping. They are not normal application GPIOs. `BSP_CAPS_BUTTONS` is
therefore zero: the generic audio example (`examples/audio`) compiles its
button handling out via `#if BSP_CAPS_BUTTONS`, so playback and recording
cannot be triggered from the board itself. See
`examples/audio/sdkconfig.bsp.candis_s31` for the resulting runtime
limitations. `bsp_spiffs_mount()` mounts the example SPIFFS partition
(`CONFIG_BSP_SPIFFS_MOUNT_POINT`, label `CONFIG_BSP_SPIFFS_PARTITION_LABEL`)
so example file content ships in the flash image.

## RTC and shared interrupt line

The RX8130CE RTC sits on the low-power I2C bus. `bsp_rtc_get_time()` and
`bsp_rtc_set_time()` handle the calendar; `bsp_rtc_set_alarm()` programs the
alarm compare fields, and `bsp_rtc_alarm_irq_enable()` switches the RTC's
active-low `/IRQ` output (`AIE`) without touching the compare settings. In
`bsp_rtc_alarm_t` each `*_en` flag includes its field in the comparison;
day-of-month and weekday are mutually exclusive because both share one
register in the RTC. With every field disabled the alarm fires once per
minute.

The RTC `/IRQ` and the TG28_SW interrupt are wired-ANDed onto
`BSP_PMIC_RTC_INT` (GPIO2, active low) through a buffer. Once
`bsp_rtc_alarm_irq_enable(true)` is in effect, a latched RTC alarm pulls
GPIO2 low and keeps it low until the flag is cleared; the line stays low
while either device has a pending event.

`bsp_shared_irq_service()` drains both devices over I2C until the line
releases and reports the combined flags. It can be polled from a task, or
driven by an interrupt: `bsp_shared_irq_register_callback()` installs a
low-level GPIO handler (edge triggering would lose an event that asserts
while the other device still holds the line low). The handler masks the
line and runs the callback in ISR context, which must only notify (for
example `xTaskNotifyFromISR()`); the actual I2C servicing still happens in
task context by calling `bsp_shared_irq_service()`, which re-arms the
interrupt before returning. Passing a NULL callback removes the handler
again. Register the callback after `bsp_board_init()`, which configures the
pin with interrupts disabled.

## Display and touch

The on-board 2.0-inch CO5300 AMOLED (QSPI, 460x460 active area inside a 470x460 GRAM window; the supplier init code sets the column window 10..469) and the CST820 capacitive touch panel (I2C, `BSP_I2C_NUM`) are both initialized by `bsp_display_start()`.

- **Sleep:** `bsp_display_enter_sleep()` / `bsp_display_exit_sleep()` put the panel into/out of sleep-in mode and put the CST820 into deep sleep. The touch controller has no wake pin, so `bsp_display_exit_sleep()` resets it over its RST GPIO and re-checks its chip ID.
- **Deep standby:** `bsp_display_enter_deep_standby()` additionally sends the CO5300 deep-standby command (RAM content lost). After `bsp_display_exit_deep_standby()` the full display pipeline is rebuilt, but LVGL widgets/screens are not recreated automatically; the application must show its screen again (e.g. `lv_screen_load()`).
- **Rotation:** `bsp_display_rotate()` rotates in software (LVGL rendering). On LVGL 9.5 the touch coordinates follow the display rotation automatically. 90/180-degree rotation may show a small offset on this panel; verify on hardware before relying on it.
- **TE limitation:** the tearing-effect pin is wired and the panel's TE output is enabled at init, but the QSPI display path cannot use TE for anti-tearing. Avoid fast full-screen scroll animations in the UI.
- **Touch is optional:** if the CST820 is missing or fails to initialize, `bsp_display_start()` still succeeds and logs a warning; the display keeps working without touch input.

See [API.md](API.md) for the full function reference.

## Camera module

The connector exposes an 8-bit DVP bus. The production camera module uses an
OV5640 with autofocus, and the checked-in camera example selects its
800 x 600 RGB565 DVP mode. Select the corresponding `esp_cam_sensor` option if
a different module is fitted.

The sensor is clocked with a 24 MHz XCLK (`BSP_CAMERA_XCLK_CLOCK_MHZ`); all
OV5640 register tables in `esp_cam_sensor` assume 24 MHz, and `bsp_camera.c`
enforces this with a compile-time check. Only the DVP video device is
initialized (`ESP_VIDEO_INIT_FLAGS_DVP`).

XCLK comes from the CAM controller, which divides it down from PLL_F160M and
drives it on `dvp_pin.xclk_io` whenever `xclk_io >= 0` and `xclk_freq > 0`
(`esp_cam_ctlr_dvp_output_clock()` in `esp_video_init.c`). No LEDC channel is
needed: the GPIO output matrix only keeps the signal attached last, so a second
source on the same pin is disconnected in practice. `CONFIG_BSP_CAMERA_XCLK_USE_LEDC`
can still route XCLK through LEDC for a diagnostic experiment and is disabled
by default; `CONFIG_BSP_CAMERA_XCLK_LEDC_CH` only applies when it is enabled.

Autofocus is not wired up in the BSP yet: a commented-out `cam_motor`
configuration block for the suspected VCM (DW9714, SCCB 0x0C, pending
module-vendor written confirmation) is preset in `bsp_camera.c` with the
enable steps (sdkconfig options + un-comment + `ESP_VIDEO_INIT_FLAGS_MOTOR`).
EVT1 units run fixed focus. See the note in `bsp_camera.c`.

## Audio codec

The ES8389 codec sits on the main I2C bus (address 0x20) and on I2S
(MCLK=GPIO35, BCLK=GPIO18, WS=GPIO19, DOUT=GPIO8, DIN=GPIO44); the speaker
amplifier enable is GPIO42. The BSP clocks the codec from MCLK
(`use_mclk=true`), which also makes `es8389_set_fs()` skip its coefficient
lookup.

`BSP_I2S_SAMPLE_RATE` is 16000 Hz and `bsp_audio_init()` uses it for its
default `i2s_std_config_t`. Keep any override inside the es8389 driver's
coefficient table (`coeff_div[]` in esp_codec_dev `~1.5`:
8000/16000/24000/32000/44100/48000/88200/96000/192000 Hz) - the previous
22050 Hz default has no row there, so `es8389_config_sample()` cannot resolve
codec clocks for it on any BCLK-clocked path.

Speaker and microphone are separate `esp_codec_dev` instances over the same
chip; whichever side is opened last soft-resets the whole codec. If both
directions are used, re-verify the speaker -> mic -> speaker sequence (see
the note in `bsp_audio.c`).

[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit&logoColor=white)](https://github.com/pre-commit/pre-commit)
