#ifndef C_KEY_TFT_H
#define C_KEY_TFT_H

#include <stdbool.h>

#include "c_key_display.h"
#include "esp_err.h"

esp_err_t c_key_tft_init(void);
bool c_key_tft_ready(void);
esp_err_t c_key_tft_render(const c_key_display_frame_t *frame);

#endif
