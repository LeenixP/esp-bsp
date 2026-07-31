/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/candis_s31.h"

typedef struct {
    const char *name;
    gpio_num_t gpio;
    uint8_t enabled_level;
} power_domain_config_t;

static const char *TAG = "candis_power";

static const power_domain_config_t s_power_domains[BSP_POWER_DOMAIN_COUNT] = {
    [BSP_POWER_TYPE_C_CONTROL] = {"type_c_control", BSP_TYPE_C_CTRL_EN,
        BSP_TYPE_C_CTRL_EN_ACTIVE_LEVEL
    },
    [BSP_POWER_DISPLAY_VBAT] = {"display_vbat", BSP_LCD_VBAT_EN,
        BSP_LCD_VBAT_EN_ACTIVE_LEVEL
    },
    [BSP_POWER_SDCARD] = {"sdcard", BSP_SD_POWER_EN,
        BSP_SD_POWER_EN_ACTIVE_LEVEL
    },
    [BSP_POWER_DISPLAY_VCI] = {"display_vci", BSP_LCD_VCI_EN,
        BSP_LCD_VCI_EN_ACTIVE_LEVEL
    },
    [BSP_POWER_AUDIO_PA] = {"audio_pa", BSP_AUDIO_PA_EN,
        BSP_AUDIO_PA_EN_ACTIVE_LEVEL
    },
    [BSP_POWER_USB_OTG] = {"usb_otg", BSP_USB_OTG_EN,
        BSP_USB_OTG_EN_ACTIVE_LEVEL
    },
};

static const char *s_peripheral_names[BSP_PERIPHERAL_COUNT] = {
    [BSP_PERIPHERAL_DISPLAY] = "display",
    [BSP_PERIPHERAL_TOUCH] = "touch",
    [BSP_PERIPHERAL_AUDIO] = "audio",
    [BSP_PERIPHERAL_CAMERA] = "camera",
    [BSP_PERIPHERAL_SDCARD] = "sdcard",
    [BSP_PERIPHERAL_EXTERNAL_3V3] = "external_3v3",
};

static bool domain_is_valid(bsp_power_domain_t domain)
{
    return domain >= 0 && domain < BSP_POWER_DOMAIN_COUNT;
}

static bool peripheral_is_valid(bsp_peripheral_t peripheral)
{
    return peripheral >= 0 && peripheral < BSP_PERIPHERAL_COUNT;
}

static esp_err_t configure_output(gpio_num_t gpio, uint32_t level)
{
    /* Load the output latch before changing direction to avoid enable pulses. */
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio, level), TAG, "GPIO latch failed");
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "GPIO config failed");
    return gpio_set_level(gpio, level);
}

/* Park the given pins as floating inputs (no pull-up/pull-down) so they
 * cannot back-feed a peripheral whose supply is about to be removed. */
static esp_err_t tristate_pins(const gpio_num_t *gpios, size_t count)
{
    uint64_t mask = 0;
    for (size_t index = 0; index < count; ++index) {
        mask |= 1ULL << gpios[index];
    }
    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t regulator_start(bsp_pmic_regulator_t regulator, uint16_t millivolts)
{
    ESP_RETURN_ON_ERROR(bsp_pmic_regulator_set_voltage(regulator, millivolts), TAG,
                        "cannot set peripheral voltage");
    return bsp_pmic_regulator_enable(regulator, true);
}

static esp_err_t camera_control_pins(void)
{
    ESP_RETURN_ON_ERROR(configure_output(BSP_CAMERA_PWDN, 1), TAG,
                        "camera PWDN config failed");
    return configure_output(BSP_CAMERA_RST, 0);
}

esp_err_t bsp_power_safe_state(void)
{
    ESP_RETURN_ON_ERROR(bsp_type_c_deinit(), TAG,
                        "Type-C controller shutdown failed");
    for (int domain = 0; domain < BSP_POWER_DOMAIN_COUNT; ++domain) {
        ESP_RETURN_ON_ERROR(bsp_power_domain_set((bsp_power_domain_t)domain, false),
                            TAG, "direct power domain disable failed");
    }
    ESP_RETURN_ON_ERROR(camera_control_pins(), TAG, "camera safe state failed");
    /* Drive the RGB data line and the touch reset line low so neither an
     * unpowered LED strip nor the touch controller is held by a high pin. */
    ESP_RETURN_ON_ERROR(configure_output(BSP_LED_RGB_IO, 0), TAG,
                        "RGB data safe state failed");
    ESP_RETURN_ON_ERROR(configure_output(BSP_TOUCH_RST, 0), TAG,
                        "touch reset safe state failed");

    /* These rails feed only optional peripherals in schematic revision 0.5. */
    const bsp_pmic_regulator_t optional_rails[] = {
        BSP_PMIC_ALDO1, BSP_PMIC_ALDO2, BSP_PMIC_ALDO3, BSP_PMIC_ALDO4,
        BSP_PMIC_BLDO1, BSP_PMIC_BLDO2, BSP_PMIC_DCDC2, BSP_PMIC_DCDC4,
    };
    ESP_RETURN_ON_ERROR(bsp_pmic_init(), TAG, "TG28_SW safe-state access failed");
    for (size_t index = 0; index < sizeof(optional_rails) / sizeof(optional_rails[0]); ++index) {
        ESP_RETURN_ON_ERROR(bsp_pmic_regulator_enable(optional_rails[index], false),
                            TAG, "TG28_SW optional rail disable failed");
    }
    return ESP_OK;
}

esp_err_t bsp_power_domain_set(bsp_power_domain_t domain, bool enable)
{
    ESP_RETURN_ON_FALSE(domain_is_valid(domain), ESP_ERR_INVALID_ARG, TAG,
                        "invalid power domain");
    const power_domain_config_t *config = &s_power_domains[domain];
    return configure_output(config->gpio,
                            enable ? config->enabled_level : !config->enabled_level);
}

esp_err_t bsp_power_domain_get(bsp_power_domain_t domain, bool *enabled)
{
    ESP_RETURN_ON_FALSE(domain_is_valid(domain) && enabled != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid power state request");
    const power_domain_config_t *config = &s_power_domains[domain];
    *enabled = gpio_get_level(config->gpio) == config->enabled_level;
    return ESP_OK;
}

const char *bsp_power_domain_name(bsp_power_domain_t domain)
{
    return domain_is_valid(domain) ? s_power_domains[domain].name : "invalid";
}

esp_err_t bsp_peripheral_power_set(bsp_peripheral_t peripheral, bool enable)
{
    ESP_RETURN_ON_FALSE(peripheral_is_valid(peripheral), ESP_ERR_INVALID_ARG, TAG,
                        "invalid peripheral");

    switch (peripheral) {
    case BSP_PERIPHERAL_DISPLAY:
        if (enable) {
            /* Power-up sequence required by the CO5300 QSPI AMOLED:
             * ALDO1 sources the VCI rail, then VBAT, then VCI after 2ms,
             * and the panel needs ~10ms before it accepts commands. */
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_ALDO1, 3300), TAG,
                                "LCD VCI source failed");
            ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_DISPLAY_VBAT, true),
                                TAG, "LCD VBAT enable failed");
            vTaskDelay(pdMS_TO_TICKS(2));
            ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_DISPLAY_VCI, true),
                                TAG, "LCD VCI enable failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            return ESP_OK;
        }
        /* Power-down is the reverse order: VCI first, then VBAT after 2ms,
         * and finally the ALDO1 source. */
        ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_DISPLAY_VCI, false), TAG,
                            "LCD VCI disable failed");
        vTaskDelay(pdMS_TO_TICKS(2));
        ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_DISPLAY_VBAT, false), TAG,
                            "LCD VBAT disable failed");
        return bsp_pmic_regulator_enable(BSP_PMIC_ALDO1, false);

    case BSP_PERIPHERAL_TOUCH:
        if (enable) {
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_ALDO2, 3300), TAG,
                                "touch rail failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            return ESP_OK;
        }
        return bsp_pmic_regulator_enable(BSP_PMIC_ALDO2, false);

    case BSP_PERIPHERAL_AUDIO:
        if (enable) {
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_ALDO3, 3300), TAG,
                                "audio rail failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_AUDIO_PA, false), TAG,
                            "audio PA disable failed");
        return bsp_pmic_regulator_enable(BSP_PMIC_ALDO3, false);

    case BSP_PERIPHERAL_CAMERA:
        if (enable) {
            ESP_RETURN_ON_ERROR(camera_control_pins(), TAG,
                                "camera control pins failed");
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_BLDO1, 2800), TAG,
                                "camera DOVDD failed");
            vTaskDelay(pdMS_TO_TICKS(1));
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_ALDO4, 2800), TAG,
                                "camera AVDD failed");
            vTaskDelay(pdMS_TO_TICKS(1));
            ESP_RETURN_ON_ERROR(regulator_start(BSP_PMIC_DCDC2, 1500), TAG,
                                "camera DVDD failed");
            vTaskDelay(pdMS_TO_TICKS(5));
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(camera_control_pins(), TAG,
                            "camera control pins failed");
        /* Hi-Z the DVP data and sync pins before removing the supplies so
         * they cannot back-feed the unpowered sensor. The video driver
         * reconfigures these pins on the next power-up. */
        const gpio_num_t camera_data_pins[] = {
            BSP_CAMERA_D0, BSP_CAMERA_D1, BSP_CAMERA_D2, BSP_CAMERA_D3,
            BSP_CAMERA_D4, BSP_CAMERA_D5, BSP_CAMERA_D6, BSP_CAMERA_D7,
            BSP_CAMERA_PCLK, BSP_CAMERA_XCLK, BSP_CAMERA_VSYNC, BSP_CAMERA_HSYNC,
        };
        ESP_RETURN_ON_ERROR(tristate_pins(camera_data_pins,
                                          sizeof(camera_data_pins) / sizeof(camera_data_pins[0])),
                            TAG, "camera data pins hi-Z failed");
        ESP_RETURN_ON_ERROR(bsp_pmic_regulator_enable(BSP_PMIC_DCDC2, false), TAG,
                            "camera DVDD disable failed");
        ESP_RETURN_ON_ERROR(bsp_pmic_regulator_enable(BSP_PMIC_ALDO4, false), TAG,
                            "camera AVDD disable failed");
        return bsp_pmic_regulator_enable(BSP_PMIC_BLDO1, false);

    case BSP_PERIPHERAL_SDCARD:
        if (enable) {
            ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_SDCARD, true), TAG,
                                "SD card power switch failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            /* Restore a neutral pin state once the rail is up: card-detect
             * gets its pull-up back and the bus pins float. The sdmmc
             * driver applies its own pin configuration when the card is
             * mounted, so this only covers the unmounted idle state. */
            const gpio_config_t detect_config = {
                .pin_bit_mask = 1ULL << BSP_SD_DET,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            ESP_RETURN_ON_ERROR(gpio_config(&detect_config), TAG,
                                "SD card-detect restore failed");
            const gpio_num_t sd_bus_pins[] = {
                BSP_SD_D0, BSP_SD_D1, BSP_SD_D2, BSP_SD_D3,
                BSP_SD_CLK, BSP_SD_CMD,
            };
            return tristate_pins(sd_bus_pins,
                                 sizeof(sd_bus_pins) / sizeof(sd_bus_pins[0]));
        }
        /* Hi-Z the card-detect and bus pins before opening the P-MOS
         * (BSP_SD_POWER_EN goes high) so they cannot back-feed the
         * unpowered card, then give the rail a moment to collapse. */
        const gpio_num_t sd_pins[] = {
            BSP_SD_DET, BSP_SD_D0, BSP_SD_D1, BSP_SD_D2, BSP_SD_D3,
            BSP_SD_CLK, BSP_SD_CMD,
        };
        ESP_RETURN_ON_ERROR(tristate_pins(sd_pins,
                                          sizeof(sd_pins) / sizeof(sd_pins[0])),
                            TAG, "SD card pins hi-Z failed");
        ESP_RETURN_ON_ERROR(bsp_power_domain_set(BSP_POWER_SDCARD, false), TAG,
                            "SD card power switch failed");
        vTaskDelay(pdMS_TO_TICKS(2));
        return ESP_OK;

    case BSP_PERIPHERAL_EXTERNAL_3V3:
        if (enable) {
            return regulator_start(BSP_PMIC_BLDO2, 3300);
        }
        return bsp_pmic_regulator_enable(BSP_PMIC_BLDO2, false);

    default:
        return ESP_ERR_INVALID_ARG;
    }
}

const char *bsp_peripheral_name(bsp_peripheral_t peripheral)
{
    return peripheral_is_valid(peripheral) ? s_peripheral_names[peripheral] : "invalid";
}
