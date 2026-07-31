#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "c_key_core.h"

static int failures;

int run_pipeline_tests(void);
int run_display_tests(void);
int run_bu03_uart2_tests(void);
int run_bu03_twr_usb_tests(void);
int run_bu04_pdoa_tests(void);
int run_bu03_bridge_tests(void);
int run_contest_scenario_tests(void);
int run_telemetry_tests(void);

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL line %d: %s\n", line, text);
        ++failures;
    }
}

static void near_impl(float actual, float expected, float tolerance, const char *text, int line)
{
    if (!isfinite(actual) || fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL line %d: %s = %.6f, expected %.6f +/- %.6f\n",
                line, text, actual, expected, tolerance);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)
#define NEAR(actual, expected, tolerance) near_impl((actual), (expected), (tolerance), #actual, __LINE__)

static float distance_between(c_key_point_t a, c_key_point_t b)
{
    return hypotf(a.x_m - b.x_m, a.y_m - b.y_m);
}

static void test_two_anchor_location(void)
{
    const c_key_point_t target = {.x_m = 0.40f, .y_m = 1.50f};
    c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT] = {
        {.position = {-0.24f, 0.00f}, .valid = true},
        {.position = {0.24f, 0.00f}, .valid = true},
    };
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        anchors[i].distance_m = distance_between(target, anchors[i].position);
    }

    c_key_point_t result;
    float residual;
    CHECK(c_key_locate_two_anchors_front(
        anchors, (c_key_point_t){0.0f, 0.0f}, 0.0f, &result, &residual));
    NEAR(result.x_m, target.x_m, 1.0e-4f);
    NEAR(result.y_m, target.y_m, 1.0e-4f);
    NEAR(residual, 0.0f, 1.0e-4f);

    const c_key_point_t behind = {.x_m = 0.10f, .y_m = -1.20f};
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        anchors[i].distance_m = distance_between(behind, anchors[i].position);
    }
    CHECK(c_key_locate_two_anchors_front(
        anchors, (c_key_point_t){0.0f, 0.0f}, 0.0f, &result, NULL));
    NEAR(result.x_m, behind.x_m, 1.0e-4f);
    NEAR(result.y_m, -behind.y_m, 1.0e-4f);

    anchors[0].distance_m = 0.10f;
    anchors[1].distance_m = 2.00f;
    CHECK(!c_key_locate_two_anchors_front(
        anchors, (c_key_point_t){0.0f, 0.0f}, 0.0f, &result, NULL));

    anchors[1].position = anchors[0].position;
    CHECK(!c_key_locate_two_anchors_front(
        anchors, (c_key_point_t){0.0f, 0.0f}, 0.0f, &result, NULL));
}

static void test_pose(void)
{
    c_key_pose_t pose;
    CHECK(c_key_pose_from_xy((c_key_point_t){0.0f, 1.3f},
                             (c_key_point_t){0.0f, 0.0f},
                             0.3f, 0.0f, 0.02f, &pose));
    NEAR(pose.boundary_distance_m, 1.0f, 1.0e-5f);
    NEAR(pose.angle_deg, 0.0f, 1.0e-5f);

    CHECK(c_key_pose_from_xy((c_key_point_t){-0.65f, 1.125833f},
                             (c_key_point_t){0.0f, 0.0f},
                             0.3f, 0.0f, 0.0f, &pose));
    NEAR(pose.boundary_distance_m, 1.0f, 1.0e-3f);
    NEAR(pose.angle_deg, -30.0f, 1.0e-2f);
}

static void test_filter_and_freshness(void)
{
    c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT] = {
        {.distance_m = 1.0f, .timestamp_ms = 950U, .valid = true},
        {.distance_m = 1.1f, .timestamp_ms = 940U, .valid = true},
    };
    CHECK(c_key_measurements_ready(anchors, 1000U, 100U, 25U));
    CHECK(!c_key_measurements_ready(anchors, 1100U, 100U, 25U));

    c_key_distance_filter_t filter;
    c_key_distance_filter_init(&filter, 0.3f);
    const float samples[] = {2.00f, 2.01f, 1.99f, 5.00f, 2.02f, 2.01f};
    float output = 0.0f;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        CHECK(c_key_distance_filter_push(&filter, samples[i], 0.05f, 10.0f, &output));
    }
    NEAR(output, 2.01f, 0.03f);
}

static void test_adaptive_angle_filter(void)
{
    c_key_angle_filter_t filter;
    c_key_angle_filter_init(&filter, 0.15f, 0.60f, 3.0f);

    const float stationary_samples[] = {0.0f, 1.8f, -1.6f, 1.2f, -1.0f, 1.4f, -1.2f};
    float output = 0.0f;
    for (size_t i = 0; i < sizeof(stationary_samples) / sizeof(stationary_samples[0]); ++i) {
        CHECK(c_key_angle_filter_push(&filter, stationary_samples[i], &output));
    }
    CHECK(fabsf(output) < 0.5f);

    for (size_t i = 0; i < 5U; ++i) {
        CHECK(c_key_angle_filter_push(&filter, 20.0f, &output));
    }
    CHECK(output > 18.0f);
    CHECK(output <= 20.0f);

    c_key_angle_filter_reset(&filter);
    CHECK(c_key_angle_filter_push(&filter, 179.0f, &output));
    CHECK(c_key_angle_filter_push(&filter, -179.0f, &output));
    CHECK(fabsf(fabsf(output) - 180.0f) < 1.0f);

    c_key_angle_filter_reset(&filter);
    CHECK(c_key_angle_filter_push(&filter, -35.0f, &output));
    NEAR(output, -35.0f, 1.0e-5f);
}

static c_key_state_input_t input_at(float distance_m, float angle_deg)
{
    return (c_key_state_input_t){true, true, 5, 5, distance_m, angle_deg};
}

static void test_state_machine(void)
{
    c_key_state_machine_t machine;
    c_key_state_machine_init(&machine, c_key_default_thresholds());
    c_key_state_input_t input = input_at(2.5f, 0.0f);

    c_key_state_machine_update(&machine, &input);
    CHECK(machine.state == C_KEY_STATE_SENSING);
    input.boundary_distance_m = 1.85f;
    CHECK(c_key_state_machine_update(&machine, &input) & C_KEY_EVENT_WELCOME_ON);
    input.boundary_distance_m = 1.98f;
    CHECK(c_key_state_machine_update(&machine, &input) == C_KEY_EVENT_NONE);
    input.boundary_distance_m = 0.85f;
    CHECK(c_key_state_machine_update(&machine, &input) & C_KEY_EVENT_UNLOCK);
    input.boundary_distance_m = 1.05f;
    CHECK(c_key_state_machine_update(&machine, &input) == C_KEY_EVENT_NONE);
    input.boundary_distance_m = 1.15f;
    CHECK(c_key_state_machine_update(&machine, &input) & C_KEY_EVENT_LOCK);
    input.boundary_distance_m = 2.15f;
    CHECK(c_key_state_machine_update(&machine, &input) & C_KEY_EVENT_WELCOME_OFF);

    input = input_at(1.5f, 0.0f);
    c_key_state_machine_update(&machine, &input);
    input.angle_deg = 48.0f;
    c_key_state_machine_update(&machine, &input);
    CHECK(machine.state == C_KEY_STATE_OUT_OF_ANGLE);
    input.angle_deg = 42.0f;
    c_key_state_machine_update(&machine, &input);
    CHECK(machine.state == C_KEY_STATE_WELCOME);
    input.accepted_id = 6;
    c_key_state_machine_update(&machine, &input);
    CHECK(machine.state == C_KEY_STATE_INVALID_ID);
    input.accepted_id = 5;
    input.measurement_valid = false;
    c_key_state_machine_update(&machine, &input);
    CHECK(machine.state == C_KEY_STATE_FAULT);
}

int main(void)
{
    test_two_anchor_location();
    test_pose();
    test_filter_and_freshness();
    test_adaptive_angle_filter();
    test_state_machine();
    failures += run_pipeline_tests();
    failures += run_display_tests();
    failures += run_bu03_uart2_tests();
    failures += run_bu03_twr_usb_tests();
    failures += run_bu04_pdoa_tests();
    failures += run_bu03_bridge_tests();
    failures += run_contest_scenario_tests();
    failures += run_telemetry_tests();
    if (failures != 0) {
        fprintf(stderr, "%d assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("All c_key_core tests passed.");
    return EXIT_SUCCESS;
}
