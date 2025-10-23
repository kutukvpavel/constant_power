idf_component_register(SRCS "main.c"
                        PRIV_REQUIRES esp_netif esp_eth nvs_flash esp_driver_gpio esp_driver_uart
                        REQUIRES my_modbus ethernet_init
                       INCLUDE_DIRS ".")
