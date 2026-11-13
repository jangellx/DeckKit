# The dev kit's RGB LED (WS2812) as a status light. Needs deckkit_util in the project too.
idf_component_register(SRCS "StatusLight.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES deckkit_util)
