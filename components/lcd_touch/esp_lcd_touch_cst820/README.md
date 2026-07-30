# ESP LCD Touch CST820 Controller

[![Component Registry](https://components.espressif.com/components/espressif/esp_lcd_touch_cst820/badge.svg)](https://components.espressif.com/components/espressif/esp_lcd_touch_cst820)

ESP-IDF driver for CST820 capacitive touch controllers. It uses the common
`esp_lcd_touch` API and communicates through I2C.

| Touch controller | Interface | Default address | Maximum points | Datasheet |
| :--------------: | :-------: | :-------------: | :------------: | :-------: |
| CST820 | I2C | `0x15` | 2 | [CST820 V1.2](https://admin.osptek.com/uploads/DS_CST_820_V1_2_e0543732ca.pdf) |

The public CST820 datasheet describes the electrical interface but does not
publish the complete touch report register map. This driver follows the
15-byte report supplied with the display module: count in byte 2 and coordinate
slots beginning at bytes 3 and 9. It does not probe CST816-family addresses,
alias CST816 registers, or fall back to a CST816 compatibility path. Driver
initialization is limited to reset and an optional ID read; it does not change
sleep, auto-sleep, or interrupt-mode registers.

If the controller is asleep or its firmware does not expose register `0xA7`,
enable `CONFIG_ESP_LCD_TOUCH_CST820_DISABLE_READ_ID`. Touch data can still be
read after the controller wakes on a touch event.

## Add the component

Add the released component to an ESP-IDF project:

```sh
idf.py add-dependency "espressif/esp_lcd_touch_cst820^1.0.0"
```

The Component Manager writes the dependency to `main/idf_component.yml` and
downloads it during the next configure or build.

## Initialize the controller

Create the I2C bus and panel IO first. The example below uses the current
ESP-IDF I2C master driver:

```c
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst820.h"

i2c_master_bus_handle_t i2c_bus = NULL;
esp_lcd_panel_io_handle_t touch_io = NULL;
esp_lcd_touch_handle_t touch = NULL;

const i2c_master_bus_config_t bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_8,
    .scl_io_num = GPIO_NUM_18,
    .clk_source = I2C_CLK_SRC_DEFAULT,
};
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

const esp_lcd_panel_io_i2c_config_t io_config =
    ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &touch_io));

const esp_lcd_touch_config_t touch_config = {
    .x_max = 460,
    .y_max = 460,
    .rst_gpio_num = GPIO_NUM_7,
    .int_gpio_num = GPIO_NUM_3,
    .levels = {
        .reset = 0,
        .interrupt = 0,
    },
    .flags = {
        .swap_xy = 0,
        .mirror_x = 0,
        .mirror_y = 0,
    },
};
ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst820(touch_io, &touch_config, &touch));
```

GPIO numbers in the example are placeholders. Use the reset, interrupt, SDA,
and SCL pins from the target board schematic. Set either reset or interrupt to
`GPIO_NUM_NC` when that signal is not connected.

The reset and interrupt active levels are provided by the board through
`touch_config.levels`; the driver does not assume a fixed module wiring.

## Read touch points

Call `esp_lcd_touch_read_data()` after an interrupt or from a periodic task,
then fetch the latest report:

```c
esp_lcd_touch_point_data_t points[2];
uint8_t point_count = 0;

ESP_ERROR_CHECK(esp_lcd_touch_read_data(touch));
ESP_ERROR_CHECK(esp_lcd_touch_get_data(touch, points, &point_count, 2));

for (uint8_t i = 0; i < point_count; i++) {
    ESP_LOGI("touch", "id=%u x=%u y=%u",
             points[i].track_id, points[i].x, points[i].y);
}
```

Set `CONFIG_ESP_LCD_TOUCH_MAX_POINTS` to at least `2` to retain both points.
The controller report provides coordinates and a tracking ID, but no documented
pressure value, so `strength` is returned as zero.

Polling is useful during early board bring-up because it does not depend on the
controller firmware's interrupt mode. Once the interrupt polarity has been
verified on hardware, an ISR callback can wake a task that performs the I2C
read. Do not access I2C directly from the ISR.

## Release resources

Delete resources in the reverse order in which they were created:

```c
ESP_ERROR_CHECK(esp_lcd_touch_del(touch));
ESP_ERROR_CHECK(esp_lcd_panel_io_del(touch_io));
ESP_ERROR_CHECK(i2c_del_master_bus(i2c_bus));
```
