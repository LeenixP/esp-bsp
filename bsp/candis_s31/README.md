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
therefore zero and the generic audio example, which expects application
buttons and SPIFFS content, is not listed.

## Camera module

The connector exposes an 8-bit DVP bus. The production camera module uses an
OV5640 with autofocus, and the checked-in camera example selects its
800 x 600 RGB565 DVP mode. Select the corresponding `esp_cam_sensor` option if
a different module is fitted.

[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit&logoColor=white)](https://github.com/pre-commit/pre-commit)
