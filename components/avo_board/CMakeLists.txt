file(GLOB SRCS "src/*.c")
# WHOLE_ARCHIVE: the HAL is called only from avo_ui, which links after us.
idf_component_register(SRCS ${SRCS}
                       WHOLE_ARCHIVE
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES avo_core avo_hal avo_ui
                                esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_tsens
                                esp_lcd esp_timer nvs_flash esp_wifi esp_netif esp_event bt
                                esp_psram spi_flash esp_hw_support esp_http_client esp-tls mbedtls
                                esp_driver_i2s esp_http_server app_update esp_partition bootloader_support
                       EMBED_TXTFILES "web/index.html")
