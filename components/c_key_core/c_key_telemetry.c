#include "c_key_telemetry.h"

#include <inttypes.h>
#include <stdio.h>

bool c_key_telemetry_format(const bu04_pdoa_frame_t *frame,
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

    const uint16_t address = frame != NULL ? frame->tag_address : 0U;
    const uint32_t distance_cm = frame != NULL ? frame->distance_cm : 0U;
    const int32_t angle_deg = frame != NULL ? frame->angle_deg : 0;
    const int written = snprintf(
        line,
        line_size,
        "C_KEY_PDOA_CSV,%" PRIu32 ",%u,%u,%u,%u,%" PRIu32
        ",%" PRId32 ",%u,%.3f,%.3f,%.3f,%.2f,%s,%" PRIu32,
        timestamp_ms,
        address,
        output->tag_id,
        accepted_id,
        uwb_link_ok ? 1U : 0U,
        distance_cm,
        angle_deg,
        output->pose_valid ? 1U : 0U,
        output->pose.position.x_m,
        output->pose.position.y_m,
        output->pose.boundary_distance_m,
        output->pose.angle_deg,
        c_key_state_name(output->state),
        output->events);
    return written >= 0 && (size_t)written < line_size;
}

bool c_key_diagnostic_format(const bu04_pdoa_frame_t *frame,
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

    const uint8_t sequence = frame != NULL ? frame->sequence : 0U;
    const uint16_t address = frame != NULL ? frame->tag_address : 0U;
    const uint32_t distance_cm = frame != NULL ? frame->distance_cm : 0U;
    const int32_t angle_deg = frame != NULL ? frame->angle_deg : 0;
    const int written = snprintf(
        line,
        line_size,
        "C_KEY_DIAG_V2,%" PRIu32 ",%u,%u,%u,%u,%u,%u,%u,"
        "%" PRIu32 ",%" PRId32 ",%.1f,%.1f,%.2f,"
        "%.3f,%.3f,%.3f,%s,%" PRIu32 ",%" PRIu32 ",%" PRIu32,
        timestamp_ms,
        sequence,
        address,
        output->tag_id,
        accepted_id,
        uwb_link_ok ? 1U : 0U,
        output->measurement_ready ? 1U : 0U,
        output->pose_valid ? 1U : 0U,
        distance_cm,
        angle_deg,
        output->corrected_distances_m[0] * 1000.0f,
        output->filtered_distances_m[0] * 1000.0f,
        output->pose.angle_deg,
        output->pose.position.x_m,
        output->pose.position.y_m,
        output->pose.boundary_distance_m,
        c_key_state_name(output->state),
        output->events,
        accepted_frames,
        rejected_frames);
    return written >= 0 && (size_t)written < line_size;
}
