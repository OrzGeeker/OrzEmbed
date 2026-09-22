idf_component_register(
    SRCS "orz_lcd.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "fonts"
    REQUIRES orz_board esp_lcd esp_driver_spi esp_driver_gpio
)
