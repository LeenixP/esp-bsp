/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "esp_check.h"
#include "esp_codec_dev_defaults.h"

#include "bsp/candis_s31.h"

static const char *TAG = "candis_audio";
static i2s_chan_handle_t s_tx_channel;
static i2s_chan_handle_t s_rx_channel;
static const audio_codec_data_if_t *s_data_if;

typedef struct {
    esp_codec_dev_handle_t device;
    const audio_codec_if_t *codec;
    const audio_codec_ctrl_if_t *control;
    const audio_codec_gpio_if_t *gpio;
} codec_instance_t;

static codec_instance_t s_speaker;
static codec_instance_t s_microphone;

static const i2s_std_gpio_config_t s_i2s_gpio = {
    .mclk = BSP_I2S_MCLK,
    .bclk = BSP_I2S_BCLK,
    .ws = BSP_I2S_LRCLK,
    .dout = BSP_I2S_DOUT,
    .din = BSP_I2S_DIN,
    .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    },
};

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (s_tx_channel != NULL && s_rx_channel != NULL && s_data_if != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_peripheral_power_set(BSP_PERIPHERAL_AUDIO, true),
                        TAG, "audio power-up failed");

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    esp_err_t error = i2s_new_channel(&channel_config, &s_tx_channel,
                                      &s_rx_channel);
    if (error != ESP_OK) {
        goto fail;
    }

    const i2s_std_config_t default_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(22050),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = s_i2s_gpio,
    };
    const i2s_std_config_t *config = i2s_config != NULL ?
                                     i2s_config : &default_config;
    error = i2s_channel_init_std_mode(s_tx_channel, config);
    if (error != ESP_OK) {
        goto fail;
    }
    error = i2s_channel_init_std_mode(s_rx_channel, config);
    if (error != ESP_OK) {
        goto fail;
    }
    error = i2s_channel_enable(s_tx_channel);
    if (error != ESP_OK) {
        goto fail;
    }
    error = i2s_channel_enable(s_rx_channel);
    if (error != ESP_OK) {
        goto fail;
    }

    audio_codec_i2s_cfg_t codec_i2s_config = {
        .port = CONFIG_BSP_I2S_NUM,
        .rx_handle = s_rx_channel,
        .tx_handle = s_tx_channel,
    };
    s_data_if = audio_codec_new_i2s_data(&codec_i2s_config);
    if (s_data_if == NULL) {
        error = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ESP_OK;

fail:
    bsp_audio_deinit();
    return error;
}

esp_err_t bsp_audio_deinit(void)
{
    esp_err_t result = ESP_OK;
    if (bsp_audio_codec_deinit(s_speaker.device) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (bsp_audio_codec_deinit(s_microphone.device) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (s_data_if != NULL) {
        if (audio_codec_delete_data_if(s_data_if) != ESP_CODEC_DEV_OK) {
            result = ESP_FAIL;
        }
        s_data_if = NULL;
    }
    if (s_tx_channel != NULL) {
        esp_err_t error = i2s_channel_disable(s_tx_channel);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            result = error;
        }
        error = i2s_del_channel(s_tx_channel);
        if (error != ESP_OK) {
            result = error;
        }
        s_tx_channel = NULL;
    }
    if (s_rx_channel != NULL) {
        esp_err_t error = i2s_channel_disable(s_rx_channel);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            result = error;
        }
        error = i2s_del_channel(s_rx_channel);
        if (error != ESP_OK) {
            result = error;
        }
        s_rx_channel = NULL;
    }
    bsp_power_domain_set(BSP_POWER_AUDIO_PA, false);
    if (bsp_peripheral_power_set(BSP_PERIPHERAL_AUDIO, false) != ESP_OK &&
            result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

const audio_codec_data_if_t *bsp_audio_get_codec_itf(void)
{
    return s_data_if;
}

static const audio_codec_ctrl_if_t *new_codec_control(void)
{
    if (bsp_i2c_init() != ESP_OK) {
        return NULL;
    }
    audio_codec_i2c_cfg_t config = {
        .port = BSP_I2C_NUM,
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    return audio_codec_new_i2c_ctrl(&config);
}

static esp_err_t delete_codec_instance(codec_instance_t *instance)
{
    esp_err_t result = ESP_OK;
    if (instance->device != NULL) {
        esp_codec_dev_delete(instance->device);
        instance->device = NULL;
    }
    if (instance->codec != NULL) {
        if (audio_codec_delete_codec_if(instance->codec) != ESP_CODEC_DEV_OK) {
            result = ESP_FAIL;
        }
        instance->codec = NULL;
    }
    if (instance->control != NULL) {
        if (audio_codec_delete_ctrl_if(instance->control) != ESP_CODEC_DEV_OK) {
            result = ESP_FAIL;
        }
        instance->control = NULL;
    }
    if (instance->gpio != NULL) {
        if (audio_codec_delete_gpio_if(instance->gpio) != ESP_CODEC_DEV_OK) {
            result = ESP_FAIL;
        }
        instance->gpio = NULL;
    }
    return result;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    if (s_speaker.device != NULL) {
        return s_speaker.device;
    }
    if (bsp_audio_init(NULL) != ESP_OK) {
        return NULL;
    }
    s_speaker.control = new_codec_control();
    s_speaker.gpio = audio_codec_new_gpio();
    if (s_speaker.control == NULL || s_speaker.gpio == NULL) {
        delete_codec_instance(&s_speaker);
        return NULL;
    }
    const esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8389_codec_cfg_t codec_config = {
        .ctrl_if = s_speaker.control,
        .gpio_if = s_speaker.gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_AUDIO_PA_EN,
        .pa_reverted = BSP_AUDIO_PA_EN_ACTIVE_LEVEL == 0,
        .master_mode = false,
        .hw_gain = gain,
    };
    s_speaker.codec = es8389_codec_new(&codec_config);
    if (s_speaker.codec == NULL) {
        delete_codec_instance(&s_speaker);
        return NULL;
    }
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_speaker.codec,
        .data_if = s_data_if,
    };
    s_speaker.device = esp_codec_dev_new(&device_config);
    if (s_speaker.device == NULL) {
        delete_codec_instance(&s_speaker);
    }
    return s_speaker.device;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    if (s_microphone.device != NULL) {
        return s_microphone.device;
    }
    if (bsp_audio_init(NULL) != ESP_OK) {
        return NULL;
    }
    s_microphone.control = new_codec_control();
    if (s_microphone.control == NULL) {
        return NULL;
    }
    es8389_codec_cfg_t codec_config = {
        .ctrl_if = s_microphone.control,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
    };
    s_microphone.codec = es8389_codec_new(&codec_config);
    if (s_microphone.codec == NULL) {
        delete_codec_instance(&s_microphone);
        return NULL;
    }
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = s_microphone.codec,
        .data_if = s_data_if,
    };
    s_microphone.device = esp_codec_dev_new(&device_config);
    if (s_microphone.device == NULL) {
        delete_codec_instance(&s_microphone);
    }
    return s_microphone.device;
}

esp_err_t bsp_audio_codec_deinit(esp_codec_dev_handle_t device)
{
    if (device == NULL) {
        return ESP_OK;
    }
    if (device == s_speaker.device) {
        return delete_codec_instance(&s_speaker);
    }
    if (device == s_microphone.device) {
        return delete_codec_instance(&s_microphone);
    }
    return ESP_ERR_NOT_FOUND;
}
