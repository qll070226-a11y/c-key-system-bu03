idf_component_register(
    SRCS "c_key_bu03_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES c_key_core esp_timer usb usb_host_cdc_acm
)
