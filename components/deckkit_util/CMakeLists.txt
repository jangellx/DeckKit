# Small helpers with no hardware of their own: checks on outside text, wrap-safe millisecond
# deadlines, and the pairing and session cryptography.
idf_component_register(SRCS "Crypto.cpp" "Text.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_timer
                       PRIV_REQUIRES mbedtls)
