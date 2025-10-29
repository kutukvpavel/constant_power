idf_component_register(SRCS "main.cpp"
                            "dbg_console.cpp"
                            "menu.cpp"
                            "params.cpp"
                            "my_hal.cpp"
                            "my_dac.cpp"
                            "modbus.cpp"
                            "my_math.cpp"
                        PRIV_REQUIRES esp_netif 
                            esp_eth 
                            nvs_flash 
                            esp_driver_gpio 
                            esp_driver_uart 
                            esp-modbus
                            console
                            spiffs
                            spi_flash
                        REQUIRES my_modbus ethernet_init my_lcd macros ESP32Encoder
                       INCLUDE_DIRS ".")
