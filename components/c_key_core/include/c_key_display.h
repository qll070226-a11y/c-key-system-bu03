#ifndef C_KEY_DISPLAY_H
#define C_KEY_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "c_key_pipeline.h"

#define C_KEY_DISPLAY_LINE_COUNT 6U
#define C_KEY_DISPLAY_LINE_LENGTH 36U

typedef struct {
    char lines[C_KEY_DISPLAY_LINE_COUNT][C_KEY_DISPLAY_LINE_LENGTH];
} c_key_display_frame_t;

bool c_key_display_format(const c_key_pipeline_output_t *pipeline,
                          uint8_t accepted_id,
                          bool uwb_link_ok,
                          c_key_display_frame_t *frame);

#endif

