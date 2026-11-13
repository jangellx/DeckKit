# Encrypted NVS, with a key in an eFuse key block (SecureStorage), from ESPDeck's SecureNVS.
idf_component_register(SRCS "SecureStorage.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_common
                       PRIV_REQUIRES nvs_flash nvs_sec_provider efuse esp_partition esp_rom mbedtls)
