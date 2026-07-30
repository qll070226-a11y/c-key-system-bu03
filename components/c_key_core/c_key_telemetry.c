#include "c_key_telemetry.h"

#include <inttypes.h>
#include <stdio.h>

bool c_key_telemetry_format(const bu03_uart2_frame_t *frame,
                            const c_key_pipeline_output_t *output,
                            uint8_t accepted_id,
                            bool uwb_link_ok,
                            uint32_t timestamp_ms,
                            char *line,
                            size_t line_size)
{
    if (output == NULL || line == NULL || line_size == 0U ||
        accepted_id > 15U) {
        return false;
    }

    const uint8_t mask = frame != NULL ? frame->valid_mask : 0U;
    const uint32_t a0_mm = frame != NULL ? frame->distance_mm[0] : 0U;
    const uint32_t a1_mm = frame != NULL ? frame->distance_mm[1] : 0U;
    const uint32_t a2_mm = frame != NULL ? frame->distance_mm[2] : 0U;
    int written;
    if (output->pose_valid) {
        written = snprintf(
            line,
            line_size,
            "C_KEY_CSV,%" PRIu32 ",%u,%u,%u,%u,%" PRIu32
            ",%" PRIu32 ",%" PRIu32 ",1,"
            "%.3f,%.3f,%.3f,%.2f,%.3f,%s,%" PRIu32,
            timestamp_ms,
            output->tag_id,
            accepted_id,
            uwb_link_ok ? 1U : 0U,
            mask,
            a0_mm,
            a1_mm,
            a2_mm,
            output->pose.position.x_m,
            output->pose.position.y_m,
            output->pose.boundary_distance_m,
            output->pose.angle_deg,
            output->pose.residual_rms_m,
            c_key_state_name(output->state),
            output->events);
    } else {
        written = snprintf(
            line,
            line_size,
            "C_KEY_CSV,%" PRIu32 ",%u,%u,%u,%u,%" PRIu32
            ",%" PRIu32 ",%" PRIu32 ",0,,,,,,%s,%" PRIu32,
            timestamp_ms,
            output->tag_id,
            accepted_id,
            uwb_link_ok ? 1U : 0U,
            mask,
            a0_mm,
            a1_mm,
            a2_mm,
            c_key_state_name(output->state),
            output->events);
    }
    return written >= 0 && (size_t)written < line_size;
}

bool c_key_diagnostic_format(const bu03_uart2_frame_t *frame,
                             const c_key_pipeline_output_t *output,
                             uint8_t accepted_id,
                             bool uwb_link_ok,
                             uint32_t timestamp_ms,
                             uint32_t accepted_frames,
                             uint32_t rejected_frames,
                             char *line,
                             size_t line_size)
{
    if (output == NULL || line == NULL || line_size == 0U ||
        accepted_id > 15U) {
        return false;
    }

    const uint8_t mask = frame != NULL ? frame->valid_mask : 0U;
    const uint32_t a0_mm = frame != NULL ? frame->distance_mm[0] : 0U;
    const uint32_t a1_mm = frame != NULL ? frame->distance_mm[1] : 0U;
    const int written = snprintf(
        line,
        line_size,
        "C_KEY_DIAG_V1,%" PRIu32 ",%" PRIu32 ",%u,%u,%u,%u,%u,%u,"
        "%" PRIu32 ",%" PRIu32 ",%.1f,%.1f,%.1f,%.1f,"
        "%.3f,%.3f,%.3f,%.2f,%.3f,%s,%" PRIu32 ",%" PRIu32 ",%" PRIu32,
        timestamp_ms,
        accepted_frames,
        output->tag_id,
        accepted_id,
        uwb_link_ok ? 1U : 0U,
        mask,
        output->measurement_ready ? 1U : 0U,
        output->pose_valid ? 1U : 0U,
        a0_mm,
        a1_mm,
        output->corrected_distances_m[0] * 1000.0f,
        output->corrected_distances_m[1] * 1000.0f,
        output->filtered_distances_m[0] * 1000.0f,
        output->filtered_distances_m[1] * 1000.0f,
        output->pose.position.x_m,
        output->pose.position.y_m,
        output->pose.boundary_distance_m,
        output->pose.angle_deg,
        output->pose.residual_rms_m,
        c_key_state_name(output->state),
        output->events,
        accepted_frames,
        rejected_frames);
    return written >= 0 && (size_t)written < line_size;
}
