#ifndef C_KEY_BU03_BRIDGE_H
#define C_KEY_BU03_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "bu03_uart2.h"
#include "c_key_pipeline.h"

bool c_key_input_from_bu03_uart2(const bu03_uart2_frame_t *frame,
                                 uint8_t tag_id,
                                 uint32_t timestamp_ms,
                                 uint16_t sequence,
                                 c_key_pipeline_input_t *input);

#endif
