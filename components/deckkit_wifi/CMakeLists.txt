# Wi-Fi on plain ESP-IDF: the station, the saved network, and Improv over serial for setting it.
# Needs deckkit_util in the project too.
idf_component_register(SRCS "ImprovProtocol.cpp" "ImprovSerial.cpp" "WiFiStation.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_netif
                       PRIV_REQUIRES deckkit_util esp_wifi esp_event esp_timer nvs_flash
                                     esp_driver_uart esp_driver_usb_serial_jtag)
