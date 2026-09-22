idf_component_register(
    SRCS "timer_app.c"
    INCLUDE_DIRS "."
    REQUIRES orz_board orz_lcd orz_rgb orz_input nvs_flash esp_timer esp_driver_gpio
)
