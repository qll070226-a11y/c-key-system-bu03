idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES c_key_bu03_usb c_key_core c_key_io c_key_tft esp_driver_uart esp_timer
)
