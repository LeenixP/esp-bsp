# TG28 switch-charger driver

This component provides an I2C driver for the switch-charger variant of the
TG28 power-management IC. It owns only the I2C device handle; the application
or board support package remains responsible for the I2C bus and for deciding
which regulator powers each peripheral.

The implemented interface covers the functions needed by Candis-S31:

- chip identification;
- VBUS, battery, charge-state, voltage, and state-of-charge readings;
- DCDC1-DCDC5, ALDO1-ALDO4, and BLDO1-BLDO2 voltage and enable control;
- interrupt status read and write-one-to-clear handling.

Voltage setters reject values that cannot be represented exactly. This keeps a
board power sequence from silently selecting a different voltage.

```c
#include "tg28_sw.h"

tg28_sw_handle_t pmic = NULL;
tg28_sw_config_t config = TG28_SW_CONFIG_DEFAULT();

ESP_ERROR_CHECK(tg28_sw_create(i2c_bus, &config, &pmic));

tg28_sw_status_t status;
ESP_ERROR_CHECK(tg28_sw_get_status(pmic, &status));

ESP_ERROR_CHECK(tg28_sw_delete(pmic));
```

The driver follows the supplier register description and Linux reference
driver. Electrical behavior must still be verified on the target board.
