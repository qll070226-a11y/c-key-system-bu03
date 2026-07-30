#include "c_key_display.h"

#include <stdio.h>
#include <string.h>

static const char *zone_name(c_key_state_t state)
{
    switch (state) {
    case C_KEY_STATE_NO_KEY:
        return "NO KEY";
    case C_KEY_STATE_INVALID_ID:
        return "INVALID ID";
    case C_KEY_STATE_OUT_OF_ANGLE:
        return "OUT OF ANGLE";
    case C_KEY_STATE_SENSING:
        return "SENSING";
    case C_KEY_STATE_WELCOME:
        return "WELCOME";
    case C_KEY_STATE_UNLOCKED:
        return "UNLOCK ZONE";
    case C_KEY_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

static const char *authentication_name(const c_key_pipeline_output_t *pipeline,
                                       uint8_t accepted_id)
{
    if (pipeline->state == C_KEY_STATE_NO_KEY) {
        return "NO KEY";
    }
    if (pipeline->tag_id > 15U || accepted_id > 15U || pipeline->tag_id != accepted_id) {
        return "DENY";
    }
    return "OK";
}

bool c_key_display_format(const c_key_pipeline_output_t *pipeline,
                          uint8_t accepted_id,
                          bool uwb_link_ok,
                          c_key_display_frame_t *frame)
{
    if (pipeline == NULL || frame == NULL || accepted_id > 15U) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    snprintf(frame->lines[0], C_KEY_DISPLAY_LINE_LENGTH,
             "KEY:%02u LOCK:%02u %s",
             pipeline->tag_id,
             accepted_id,
             authentication_name(pipeline, accepted_id));

    if (pipeline->pose_valid) {
        snprintf(frame->lines[1], C_KEY_DISPLAY_LINE_LENGTH,
                 "D:%5.2fm A:%+5.1fdeg",
                 pipeline->pose.boundary_distance_m,
                 pipeline->pose.angle_deg);
        snprintf(frame->lines[2], C_KEY_DISPLAY_LINE_LENGTH,
                 "X:%+5.2f Y:%+5.2f",
                 pipeline->pose.position.x_m,
                 pipeline->pose.position.y_m);
    } else {
        snprintf(frame->lines[1], C_KEY_DISPLAY_LINE_LENGTH, "D: --.--m A: --.-deg");
        snprintf(frame->lines[2], C_KEY_DISPLAY_LINE_LENGTH, "X: --.-- Y: --.--");
    }

    snprintf(frame->lines[3], C_KEY_DISPLAY_LINE_LENGTH,
             "ZONE:%s", zone_name(pipeline->state));
    snprintf(frame->lines[4], C_KEY_DISPLAY_LINE_LENGTH,
             "LOCK:%s WELCOME:%s",
             pipeline->unlocked_output ? "OPEN" : "CLOSED",
             pipeline->welcome_output ? "ON" : "OFF");

    if (pipeline->pose_valid) {
        snprintf(frame->lines[5], C_KEY_DISPLAY_LINE_LENGTH,
                 "UWB:%s RES:%4.2fm",
                 uwb_link_ok ? "OK" : "LOST",
                 pipeline->pose.residual_rms_m);
    } else {
        snprintf(frame->lines[5], C_KEY_DISPLAY_LINE_LENGTH,
                 "UWB:%s RES:--.--",
                 uwb_link_ok ? "OK" : "LOST");
    }
    return true;
}

