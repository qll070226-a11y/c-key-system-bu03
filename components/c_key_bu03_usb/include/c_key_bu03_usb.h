#ifndef C_KEY_BU03_USB_H
#define C_KEY_BU03_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool device_connected;
    bool tag_id_valid;
    uint16_t latest_tag_id;
    uint32_t last_tag_id_ms;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
} c_key_bu03_usb_stats_t;

esp_err_t c_key_bu03_usb_start(void);

bool c_key_bu03_usb_get_recent_tag_id(uint32_t current_ms,
                                     uint32_t maximum_age_ms,
                                     uint8_t *tag_id);

void c_key_bu03_usb_get_stats(c_key_bu03_usb_stats_t *stats);

#endif
