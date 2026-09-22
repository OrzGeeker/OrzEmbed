idf_component_register(
    SRCS "hwtest.c"
    INCLUDE_DIRS "."
    REQUIRES orz_board orz_lcd orz_rgb
             nvs_flash esp_wifi esp_netif
             esp_driver_spi esp_driver_gpio esp_driver_sdspi
             sdmmc fatfs vfs spi_flash esp_timer
)
