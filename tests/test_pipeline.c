#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "c_key_pipeline.h"

static int pipeline_failures;

static void pipeline_check(bool condition, int line)
{
    if (!condition) {
        fprintf(stderr, "PIPELINE FAIL line %d\n", line);
        ++pipeline_failures;
    }
}

#define PCHECK(condition) pipeline_check((condition), __LINE__)

static float range_to(c_key_point_t a, c_key_point_t b)
{
    return hypotf(a.x_m - b.x_m, a.y_m - b.y_m);
}

static c_key_pipeline_config_t make_config(void)
{
    return (c_key_pipeline_config_t){
        .anchor_positions = {
            {-0.24f, 0.00f},
            {0.24f, 0.00f},
        },
        .distance_scale_factors = {1.0f, 1.0f},
        .door_center = {0.0f, 0.0f},
        .door_radius_m = 0.30f,
        .front_angle_offset_deg = 0.0f,
        .filter_alpha = 0.30f,
        .angle_one_euro_min_cutoff_hz = 0.8f,
        .angle_one_euro_beta = 0.03f,
        .angle_one_euro_derivative_cutoff_hz = 1.0f,
        .angle_hampel_sigma = 3.0f,
        .angle_hampel_min_threshold_deg = 12.0f,
        .angle_hampel_max_rejections = 3U,
        .minimum_distance_m = 0.05f,
        .maximum_distance_m = 10.0f,
        .maximum_residual_m = 0.05f,
        .maximum_age_ms = 100U,
        .maximum_skew_ms = 30U,
        .accepted_id = 5U,
        .thresholds = {
            .unlock_enter_m = 0.95f,
            .unlock_exit_m = 1.05f,
            .welcome_enter_m = 1.95f,
            .welcome_exit_m = 2.05f,
            .angle_enter_abs_deg = 43.0f,
            .angle_exit_abs_deg = 47.0f,
            .transition_confirm_frames = 4U,
        },
    };
}

static void test_scale_and_offset_calibration(void)
{
    c_key_pipeline_config_t config = make_config();
    config.distance_scale_factors[0] = 1.031273f;
    config.distance_scale_factors[1] = 0.995319f;
    config.distance_offsets_m[0] = -0.086617f;
    config.distance_offsets_m[1] = -0.045026f;

    c_key_pipeline_t pipeline;
    PCHECK(c_key_pipeline_init(&pipeline, &config));

    const c_key_point_t target = {0.0f, 2.30f};
    c_key_pipeline_input_t input = {
        .signal_present = true,
        .tag_id = 5U,
        .now_ms = 1000U,
    };
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        const float calibrated_range = range_to(target, config.anchor_positions[i]);
        input.anchors[i].distance_m =
            (calibrated_range - config.distance_offsets_m[i]) /
            config.distance_scale_factors[i];
        input.anchors[i].timestamp_ms = 1000U;
        input.anchors[i].sequence = 1U;
        input.anchors[i].valid = true;
    }

    c_key_pipeline_output_t output;
    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(output.pose_valid);
    PCHECK(fabsf(output.pose.position.x_m) < 1.0e-3f);
    PCHECK(fabsf(output.pose.position.y_m - 2.30f) < 1.0e-3f);

    config.distance_scale_factors[0] = 0.0f;
    PCHECK(!c_key_pipeline_init(&pipeline, &config));
}

static void test_pdoa_direct_pose(void)
{
    c_key_pipeline_config_t config = make_config();
    config.front_angle_offset_deg = 5.0f;
    c_key_pipeline_t pipeline;
    PCHECK(c_key_pipeline_init(&pipeline, &config));

    c_key_pdoa_input_t input = {
        .signal_present = true,
        .measurement_valid = true,
        .tag_id = 5U,
        .now_ms = 1000U,
        .sequence = 7U,
        .distance_m = 1.30f,
        .angle_deg = 35.0f,
    };
    c_key_pipeline_output_t output;
    for (size_t i = 0; i < 8U; ++i) {
        input.now_ms = 1000U + (uint32_t)i * 20U;
        input.sequence = (uint16_t)(7U + i);
        PCHECK(c_key_pipeline_process_pdoa(&pipeline, &input, &output));
        if (i < 4U) {
            PCHECK(!output.measurement_ready);
            PCHECK(!output.pose_valid);
            PCHECK(output.state == C_KEY_STATE_NO_KEY);
        }
    }
    PCHECK(output.measurement_ready);
    PCHECK(output.pose_valid);
    PCHECK(fabsf(output.pose.center_distance_m - 1.30f) < 1.0e-3f);
    PCHECK(fabsf(output.pose.boundary_distance_m - 1.00f) < 1.0e-3f);
    PCHECK(fabsf(output.pose.angle_deg - 30.0f) < 1.0e-3f);
    PCHECK(fabsf(output.pose.position.x_m - 0.65f) < 1.0e-3f);
    PCHECK(fabsf(output.pose.position.y_m - 1.125833f) < 1.0e-3f);
    PCHECK(output.state == C_KEY_STATE_WELCOME);

    c_key_pdoa_input_t missing = input;
    missing.signal_present = false;
    PCHECK(c_key_pipeline_process_pdoa(&pipeline, &missing, &output));
    PCHECK(output.state == C_KEY_STATE_NO_KEY);
    PCHECK(!output.pose_valid);

    for (size_t i = 0; i < 4U; ++i) {
        input.now_ms += 20U;
        ++input.sequence;
        PCHECK(c_key_pipeline_process_pdoa(&pipeline, &input, &output));
        PCHECK(!output.measurement_ready);
        PCHECK(!output.pose_valid);
        PCHECK(output.state == C_KEY_STATE_NO_KEY);
    }
    input.now_ms += 20U;
    ++input.sequence;
    PCHECK(c_key_pipeline_process_pdoa(&pipeline, &input, &output));
    PCHECK(output.measurement_ready);
    PCHECK(output.pose_valid);
    PCHECK(output.state == C_KEY_STATE_NO_KEY);

    c_key_pdoa_input_t invalid = input;
    invalid.measurement_valid = false;
    ++invalid.now_ms;
    ++invalid.sequence;
    PCHECK(c_key_pipeline_process_pdoa(&pipeline, &invalid, &output));
    PCHECK(output.state == C_KEY_STATE_FAULT);
    PCHECK(!output.pose_valid);
}

int run_pipeline_tests(void)
{
    test_scale_and_offset_calibration();
    test_pdoa_direct_pose();
    c_key_pipeline_config_t config = make_config();
    c_key_pipeline_t pipeline;
    PCHECK(c_key_pipeline_init(&pipeline, &config));

    const c_key_point_t target = {0.0f, 1.15f};
    c_key_pipeline_input_t input = {
        .signal_present = true,
        .tag_id = 5U,
        .now_ms = 1020U,
    };
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        input.anchors[i].distance_m = range_to(target, config.anchor_positions[i]);
        input.anchors[i].timestamp_ms = 1000U;
        input.anchors[i].sequence = 1U;
        input.anchors[i].valid = true;
    }

    c_key_pipeline_output_t output;
    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(output.measurement_ready);
    PCHECK(output.pose_valid);
    PCHECK(fabsf(output.pose.boundary_distance_m - 0.85f) < 1.0e-3f);
    PCHECK(fabsf(output.pose.angle_deg) < 1.0e-3f);
    PCHECK(output.state == C_KEY_STATE_NO_KEY);
    PCHECK(output.events == C_KEY_EVENT_NONE);

    uint32_t startup_events = output.events;
    for (size_t i = 0; i < 3U; ++i) {
        PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
        startup_events |= output.events;
    }
    PCHECK(output.state == C_KEY_STATE_UNLOCKED);
    PCHECK((startup_events & C_KEY_EVENT_WELCOME_ON) != 0U);
    PCHECK((startup_events & C_KEY_EVENT_UNLOCK) != 0U);

    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(output.events == C_KEY_EVENT_NONE);

    PCHECK(c_key_pipeline_set_accepted_id(&pipeline, 6U));
    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(output.state == C_KEY_STATE_INVALID_ID);
    PCHECK((output.events & C_KEY_EVENT_LOCK) != 0U);
    PCHECK((output.events & C_KEY_EVENT_WELCOME_OFF) != 0U);
    PCHECK(!c_key_pipeline_set_accepted_id(&pipeline, 16U));

    PCHECK(c_key_pipeline_set_accepted_id(&pipeline, 5U));
    input.now_ms = 1200U;
    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(!output.measurement_ready);
    PCHECK(output.state == C_KEY_STATE_FAULT);

    input.signal_present = false;
    PCHECK(c_key_pipeline_process(&pipeline, &input, &output));
    PCHECK(output.state == C_KEY_STATE_NO_KEY);

    config.anchor_positions[1] = config.anchor_positions[0];
    PCHECK(!c_key_pipeline_init(&pipeline, &config));
    return pipeline_failures;
}
