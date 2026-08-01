#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "c_key_pipeline.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL contest line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

typedef struct {
    uint32_t timestamp_ms;
    uint16_t sequence;
} scenario_clock_t;

static float range_to(c_key_point_t a, c_key_point_t b)
{
    return hypotf(a.x_m - b.x_m, a.y_m - b.y_m);
}

static c_key_pipeline_config_t make_config(void)
{
    return (c_key_pipeline_config_t){
        .anchor_positions = {
            {-0.2200f, 0.0000f},
            {0.2200f, 0.0000f},
        },
        .distance_scale_factors = {1.0f, 1.0f},
        .door_center = {0.0f, 0.0f},
        .door_radius_m = 0.30f,
        .filter_alpha = 0.35f,
        .angle_filter_stationary_alpha = 0.15f,
        .angle_filter_moving_alpha = 0.60f,
        .angle_filter_motion_threshold_deg = 3.0f,
        .minimum_distance_m = 0.10f,
        .maximum_distance_m = 20.0f,
        .maximum_residual_m = 0.35f,
        .maximum_age_ms = 500U,
        .maximum_skew_ms = 0U,
        .accepted_id = 5U,
        .thresholds = {
            .unlock_enter_m = 0.95f,
            .unlock_exit_m = 1.05f,
            .welcome_enter_m = 1.95f,
            .welcome_exit_m = 2.05f,
            .angle_enter_abs_deg = 43.0f,
            .angle_exit_abs_deg = 47.0f,
        },
    };
}

static uint32_t settle_at(c_key_pipeline_t *pipeline,
                          const c_key_pipeline_config_t *config,
                          c_key_point_t target,
                          uint8_t tag_id,
                          size_t frame_count,
                          scenario_clock_t *clock,
                          c_key_pipeline_output_t *output)
{
    uint32_t events = C_KEY_EVENT_NONE;
    for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        c_key_pipeline_input_t input = {
            .signal_present = true,
            .tag_id = tag_id,
            .now_ms = clock->timestamp_ms,
        };
        for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
            input.anchors[i].distance_m =
                range_to(target, config->anchor_positions[i]);
            input.anchors[i].timestamp_ms = clock->timestamp_ms;
            input.anchors[i].sequence = clock->sequence;
            input.anchors[i].valid = true;
        }
        CHECK(c_key_pipeline_process(pipeline, &input, output));
        events |= output->events;
        clock->timestamp_ms += 50U;
        ++clock->sequence;
    }
    return events;
}

static c_key_point_t polar_target(float boundary_distance_m, float angle_deg)
{
    const float center_distance_m = boundary_distance_m + 0.30f;
    const float radians = angle_deg * 3.14159265358979323846f / 180.0f;
    return (c_key_point_t){
        .x_m = center_distance_m * sinf(radians),
        .y_m = center_distance_m * cosf(radians),
    };
}

static void test_full_approach_and_departure(void)
{
    const c_key_pipeline_config_t config = make_config();
    c_key_pipeline_t pipeline;
    c_key_pipeline_output_t output;
    scenario_clock_t clock = {.timestamp_ms = 1000U, .sequence = 1U};
    CHECK(c_key_pipeline_init(&pipeline, &config));

    settle_at(&pipeline, &config, polar_target(3.00f, 0.0f),
              5U, 30U, &clock, &output);
    CHECK(output.pose_valid);
    CHECK(fabsf(output.pose.boundary_distance_m - 3.00f) < 0.01f);
    CHECK(output.state == C_KEY_STATE_SENSING);

    const uint32_t welcome_events =
        settle_at(&pipeline, &config, polar_target(1.75f, 0.0f),
                  5U, 40U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_WELCOME);
    CHECK((welcome_events & C_KEY_EVENT_WELCOME_ON) != 0U);

    const uint32_t unlock_events =
        settle_at(&pipeline, &config, polar_target(0.80f, 0.0f),
                  5U, 40U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_UNLOCKED);
    CHECK((unlock_events & C_KEY_EVENT_UNLOCK) != 0U);

    for (size_t i = 0; i < 30U; ++i) {
        const float jittered_distance = 1.00f + (i % 2U == 0U ? 0.04f : -0.04f);
        settle_at(&pipeline, &config, polar_target(jittered_distance, 0.0f),
                  5U, 1U, &clock, &output);
        CHECK(output.state == C_KEY_STATE_UNLOCKED);
    }

    const uint32_t lock_events =
        settle_at(&pipeline, &config, polar_target(1.25f, 0.0f),
                  5U, 40U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_WELCOME);
    CHECK((lock_events & C_KEY_EVENT_LOCK) != 0U);
    CHECK(output.welcome_output);

    const uint32_t leave_events =
        settle_at(&pipeline, &config, polar_target(2.25f, 0.0f),
                  5U, 40U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_SENSING);
    CHECK((leave_events & C_KEY_EVENT_WELCOME_OFF) != 0U);
    CHECK(!output.unlocked_output);
    CHECK(!output.welcome_output);
}

static void test_angle_id_and_loss(void)
{
    const c_key_pipeline_config_t config = make_config();
    c_key_pipeline_t pipeline;
    c_key_pipeline_output_t output;
    scenario_clock_t clock = {.timestamp_ms = 500U, .sequence = 1U};
    CHECK(c_key_pipeline_init(&pipeline, &config));

    settle_at(&pipeline, &config, polar_target(1.50f, 50.0f),
              5U, 30U, &clock, &output);
    CHECK(output.pose_valid);
    CHECK(fabsf(output.pose.angle_deg - 50.0f) < 0.1f);
    CHECK(output.state == C_KEY_STATE_OUT_OF_ANGLE);

    settle_at(&pipeline, &config, polar_target(1.50f, 40.0f),
              5U, 40U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_WELCOME);

    CHECK(c_key_pipeline_set_accepted_id(&pipeline, 6U));
    settle_at(&pipeline, &config, polar_target(0.50f, 0.0f),
              5U, 30U, &clock, &output);
    CHECK(output.state == C_KEY_STATE_INVALID_ID);
    CHECK(!output.unlocked_output);

    const c_key_pipeline_input_t lost = {
        .signal_present = false,
        .tag_id = 5U,
        .now_ms = clock.timestamp_ms,
    };
    CHECK(c_key_pipeline_process(&pipeline, &lost, &output));
    CHECK(output.state == C_KEY_STATE_NO_KEY);
    CHECK(!output.unlocked_output);
}

int run_contest_scenario_tests(void)
{
    test_full_approach_and_departure();
    test_angle_id_and_loss();
    return failures;
}
