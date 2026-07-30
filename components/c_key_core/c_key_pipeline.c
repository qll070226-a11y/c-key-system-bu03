#include "c_key_pipeline.h"

#include <math.h>
#include <string.h>

static bool positions_are_usable(const c_key_point_t positions[C_KEY_ANCHOR_COUNT])
{
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        if (!isfinite(positions[i].x_m) || !isfinite(positions[i].y_m)) {
            return false;
        }
    }

    const float baseline_x = positions[1].x_m - positions[0].x_m;
    const float baseline_y = positions[1].y_m - positions[0].y_m;
    return hypotf(baseline_x, baseline_y) >= 0.10f;
}

static bool config_is_valid(const c_key_pipeline_config_t *config)
{
    if (config == NULL || !positions_are_usable(config->anchor_positions) ||
        !isfinite(config->door_center.x_m) || !isfinite(config->door_center.y_m) ||
        !isfinite(config->door_radius_m) || config->door_radius_m < 0.0f ||
        !isfinite(config->front_angle_offset_deg) ||
        !isfinite(config->filter_alpha) || config->filter_alpha <= 0.0f ||
        config->filter_alpha > 1.0f ||
        !isfinite(config->minimum_distance_m) || !isfinite(config->maximum_distance_m) ||
        config->minimum_distance_m < 0.0f ||
        config->minimum_distance_m >= config->maximum_distance_m ||
        !isfinite(config->maximum_residual_m) || config->maximum_residual_m <= 0.0f ||
        config->maximum_age_ms == 0U || config->accepted_id > 15U) {
        return false;
    }

    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        if (!isfinite(config->distance_scale_factors[i]) ||
            config->distance_scale_factors[i] < 0.5f ||
            config->distance_scale_factors[i] > 1.5f ||
            !isfinite(config->distance_offsets_m[i])) {
            return false;
        }
    }
    return true;
}

bool c_key_pipeline_init(c_key_pipeline_t *pipeline,
                         const c_key_pipeline_config_t *config)
{
    if (pipeline == NULL || !config_is_valid(config)) {
        return false;
    }

    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->config = *config;
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        c_key_distance_filter_init(&pipeline->filters[i], config->filter_alpha);
    }
    c_key_state_machine_init(&pipeline->state_machine, config->thresholds);
    return true;
}

bool c_key_pipeline_set_accepted_id(c_key_pipeline_t *pipeline, uint8_t accepted_id)
{
    if (pipeline == NULL || accepted_id > 15U) {
        return false;
    }
    pipeline->config.accepted_id = accepted_id;
    return true;
}

static void update_output_state(const c_key_pipeline_t *pipeline,
                                c_key_pipeline_output_t *output)
{
    output->state = pipeline->state_machine.state;
    output->welcome_output = pipeline->state_machine.welcome_output;
    output->unlocked_output = pipeline->state_machine.unlocked_output;
}

bool c_key_pipeline_process(c_key_pipeline_t *pipeline,
                            const c_key_pipeline_input_t *input,
                            c_key_pipeline_output_t *output)
{
    if (pipeline == NULL || input == NULL || output == NULL) {
        return false;
    }

    memset(output, 0, sizeof(*output));
    output->tag_id = input->tag_id;

    c_key_state_input_t state_input = {
        .signal_present = input->signal_present,
        .measurement_valid = false,
        .tag_id = input->tag_id,
        .accepted_id = pipeline->config.accepted_id,
    };

    if (!input->signal_present) {
        output->events = c_key_state_machine_update(&pipeline->state_machine, &state_input);
        update_output_state(pipeline, output);
        return true;
    }

    c_key_anchor_measurement_t corrected[C_KEY_ANCHOR_COUNT];
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        corrected[i] = input->anchors[i];
        corrected[i].position = pipeline->config.anchor_positions[i];
        corrected[i].distance_m =
            corrected[i].distance_m * pipeline->config.distance_scale_factors[i] +
            pipeline->config.distance_offsets_m[i];
    }

    const bool synchronized = c_key_measurements_ready(
        corrected, input->now_ms, pipeline->config.maximum_age_ms, pipeline->config.maximum_skew_ms);

    bool all_filters_valid = synchronized;
    if (synchronized) {
        for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
            const bool new_sample = !pipeline->sample_seen[i] ||
                                    corrected[i].timestamp_ms != pipeline->last_timestamps_ms[i] ||
                                    corrected[i].sequence != pipeline->last_sequences[i];
            if (new_sample) {
                pipeline->filter_valid[i] = c_key_distance_filter_push(
                    &pipeline->filters[i],
                    corrected[i].distance_m,
                    pipeline->config.minimum_distance_m,
                    pipeline->config.maximum_distance_m,
                    &pipeline->filtered_distances_m[i]);
                pipeline->last_timestamps_ms[i] = corrected[i].timestamp_ms;
                pipeline->last_sequences[i] = corrected[i].sequence;
                pipeline->sample_seen[i] = true;
            }
            all_filters_valid = all_filters_valid && pipeline->filter_valid[i];
            output->filtered_distances_m[i] = pipeline->filtered_distances_m[i];
        }
    }
    output->measurement_ready = all_filters_valid;

    if (all_filters_valid) {
        c_key_anchor_measurement_t filtered[C_KEY_ANCHOR_COUNT];
        for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
            filtered[i] = corrected[i];
            filtered[i].distance_m = pipeline->filtered_distances_m[i];
            filtered[i].valid = true;
        }

        c_key_point_t position;
        float residual = 0.0f;
        if (c_key_locate_two_anchors_front(filtered,
                                           pipeline->config.door_center,
                                           pipeline->config.front_angle_offset_deg,
                                           &position,
                                           &residual) &&
            residual <= pipeline->config.maximum_residual_m &&
            c_key_pose_from_xy(position,
                               pipeline->config.door_center,
                               pipeline->config.door_radius_m,
                               pipeline->config.front_angle_offset_deg,
                               residual,
                               &output->pose)) {
            output->pose_valid = true;
            state_input.measurement_valid = true;
            state_input.boundary_distance_m = output->pose.boundary_distance_m;
            state_input.angle_deg = output->pose.angle_deg;
        }
    }

    output->events = c_key_state_machine_update(&pipeline->state_machine, &state_input);
    update_output_state(pipeline, output);
    return true;
}
