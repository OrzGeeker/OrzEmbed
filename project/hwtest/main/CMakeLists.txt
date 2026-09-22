idf_component_register(
    SRCS "hwtest.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_lcd esp_driver_spi esp_driver_gpio esp_driver_rmt esp_driver_sdspi sdmmc fatfs vfs spi_flash esp_timer
)
