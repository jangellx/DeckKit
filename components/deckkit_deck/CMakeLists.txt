# A Stream Deck on the ESP32-S3's USB host port, and the key images the firmware draws itself.
idf_component_register(SRCS "KeyImage.cpp" "MiraboxProtocol.cpp" "StreamDeck.cpp" "StreamDeckMirabox.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES usb esp_timer)
