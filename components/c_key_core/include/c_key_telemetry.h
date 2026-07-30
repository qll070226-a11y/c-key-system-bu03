#ifndef C_KEY_TELEMETRY_H
#define C_KEY_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bu03_uart2.h"
#include "c_key_pipeline.h"

#define C_KEY_TELEMETRY_LINE_LENGTH 256U

bool c_key_telemetry_format(const bu03_uart2_frame_t *frame,
                            const c_key_pipeline_output_t *output,
                            uint8_t accepted_id,
                            bool uwb_link_ok,
                            uint32_t timestamp_ms,
                            char *line,
                            size_t line_size);

#endif
