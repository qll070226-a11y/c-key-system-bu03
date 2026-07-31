#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "c_key_display.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL display line %d: %s\\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static void test_valid_welcome_frame(void)
{
    const c_key_pipeline_output_t pipeline = {
        .tag_id = 5U,
        .pose_valid = true,
        .pose = {
            .position = {.x_m = 0.25f, .y_m = 1.80f},
            .boundary_distance_m = 1.50f,
            .angle_deg = 8.0f,
            .residual_rms_m = 0.03f,
        },
        .state = C_KEY_STATE_WELCOME,
        .welcome_output = true,
        .unlocked_output = false,
    };
    c_key_display_frame_t frame;

    CHECK(c_key_display_format(&pipeline, 5U, true, &frame));
    CHECK(strcmp(frame.lines[0], "KEY:05 LOCK:05 OK") == 0);
    CHECK(strstr(frame.lines[1], "D: 1.50m") != NULL);
    CHECK(strcmp(frame.lines[3], "ZONE:WELCOME") == 0);
    CHECK(strcmp(frame.lines[4], "LOCK:CLOSED WELCOME:ON") == 0);
    CHECK(strstr(frame.lines[5], "UWB:OK") != NULL);
    CHECK(frame.tag_id == 5U);
    CHECK(frame.accepted_id == 5U);
    CHECK(frame.key_present);
    CHECK(frame.authenticated);
    CHECK(frame.pose_valid);
    CHECK(frame.uwb_link_ok);
    CHECK(frame.state == C_KEY_STATE_WELCOME);
    CHECK(frame.welcome_output);
    CHECK(!frame.unlocked_output);
}

static void test_invalid_frame_without_pose(void)
{
    const c_key_pipeline_output_t pipeline = {
        .tag_id = 7U,
        .pose_valid = false,
        .state = C_KEY_STATE_INVALID_ID,
    };
    c_key_display_frame_t frame;

    CHECK(c_key_display_format(&pipeline, 5U, false, &frame));
    CHECK(strcmp(frame.lines[0], "KEY:07 LOCK:05 DENY") == 0);
    CHECK(strcmp(frame.lines[1], "D: --.--m A: --.-deg") == 0);
    CHECK(strcmp(frame.lines[3], "ZONE:INVALID ID") == 0);
    CHECK(strcmp(frame.lines[5], "UWB:LOST RES:--.--") == 0);
    CHECK(frame.key_present);
    CHECK(!frame.authenticated);
    CHECK(!frame.pose_valid);
    CHECK(!frame.uwb_link_ok);
}

static void test_arguments_and_termination(void)
{
    const c_key_pipeline_output_t pipeline = {0};
    c_key_display_frame_t frame;

    CHECK(!c_key_display_format(NULL, 0U, true, &frame));
    CHECK(!c_key_display_format(&pipeline, 16U, true, &frame));
    CHECK(!c_key_display_format(&pipeline, 0U, true, NULL));
    CHECK(c_key_display_format(&pipeline, 0U, true, &frame));
    CHECK(strcmp(frame.lines[0], "KEY:-- LOCK:00 NO KEY") == 0);
    CHECK(!frame.key_present);
    for (size_t i = 0; i < C_KEY_DISPLAY_LINE_COUNT; ++i) {
        CHECK(frame.lines[i][C_KEY_DISPLAY_LINE_LENGTH - 1U] == '\0');
    }
}

int run_display_tests(void)
{
    test_valid_welcome_frame();
    test_invalid_frame_without_pose();
    test_arguments_and_termination();
    return failures;
}
