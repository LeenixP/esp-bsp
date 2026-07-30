file(GLOB_RECURSE BSP_SOURCES "src/*.c")

idf_component_register(
    SRCS ${BSP_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_sdmmc
        esp_driver_sdspi
        esp_driver_spi
        esp_lcd
        esp_lcd_touch
        fatfs
        vfs
    PRIV_REQUIRES
        esp_psram
        lwip
        tg28_sw
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
