#ifndef C_KEY_IO_H
#define C_KEY_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "c_key_core.h"
#include "esp_err.h"

typedef struct {
    uint8_t candidate_id;
    uint8_t stable_id;
    uint32_t candidate_since_ms;
    uint32_t buzzer_until_ms;
    bool dip_initialized;
    bool buzzer_active;
    bool initialized;
} c_key_io_t;

esp_err_t c_key_io_init(c_key_io_t *io);

bool c_key_io_dip_available(void);

bool c_key_io_poll_accepted_id(c_key_io_t *io,
                               uint32_t now_ms,
                               uint8_t *accepted_id,
                               bool *changed);

void c_key_io_apply_state(c_key_io_t *io,
                          c_key_state_t state,
                          uint32_t events,
                          uint32_t now_ms);

void c_key_io_force_safe(c_key_io_t *io);

#endif

