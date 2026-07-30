idf_component_register(
    SRCS c_key_tft.c
    INCLUDE_DIRS include
    REQUIRES c_key_core esp_driver_gpio esp_driver_spi
)
