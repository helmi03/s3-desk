idf_component_register(
    SRCS app_main.c app.c audio.c clock.c console.c display.c fetch.c hw.c net.c power.c storage.c tables.c web.c
    INCLUDE_DIRS .
    EMBED_TXTFILES index.html
    PRIV_REQUIRES core ui json nvs_flash esp_wifi esp_netif esp_http_client esp_http_server esp_https_ota
                  app_update esp_app_format console mbedtls esp_timer esp_pm esp_driver_i2c spiffs vfs)
spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)
