idf_component_register(
    SRCS "timer_app.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_rmt nvs_flash esp_timer
)
