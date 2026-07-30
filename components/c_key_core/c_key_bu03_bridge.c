#include "c_key_bu03_bridge.h"

#include <string.h>

bool c_key_input_from_bu03_uart2(const bu03_uart2_frame_t *frame,
                                 uint8_t tag_id,
                                 uint32_t timestamp_ms,
                                 uint16_t sequence,
                                 c_key_pipeline_input_t *input)
{
    if (frame == NULL || input == NULL || tag_id > 15U) {
        return false;
    }

    memset(input, 0, sizeof(*input));
    input->tag_id = tag_id;
    input->now_ms = timestamp_ms;
    const uint8_t used_anchor_mask =
        (uint8_t)((1U << C_KEY_ANCHOR_COUNT) - 1U);
    input->signal_present = (frame->valid_mask & used_anchor_mask) != 0U;
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        input->anchors[i].distance_m = (float)frame->distance_mm[i] * 0.001f;
        input->anchors[i].timestamp_ms = timestamp_ms;
        input->anchors[i].sequence = sequence;
        input->anchors[i].valid = (frame->valid_mask & (uint8_t)(1U << i)) != 0U;
    }
    return true;
}
