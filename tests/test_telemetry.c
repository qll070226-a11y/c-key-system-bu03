#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "c_key_telemetry.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL telemetry line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static size_t comma_count(const char *text)
{
    size_t count = 0U;
    while (*text != '\0') {
        if (*text++ == ',') {
            ++count;
        }
    }
    return count;
}

static void test_pose_line(void)
{
    const bu03_uart2_frame_t frame = {
        .valid_mask = 0x03U,
        .distance_mm = {1430U, 860U, 1250U},
    };
    const c_key_pipeline_output_t output = {
        .pose_valid = true,
        .tag_id = 5U,
        .pose = {
            .position = {.x_m = 0.25f, .y_m = 1.80f},
            .boundary_distance_m = 1.50f,
            .angle_deg = 8.0f,
            .residual_rms_m = 0.03f,
        },
        .state = C_KEY_STATE_WELCOME,
        .events = C_KEY_EVENT_WELCOME_ON,
    };
    char line[C_KEY_TELEMETRY_LINE_LENGTH];

    CHECK(c_key_telemetry_format(
        &frame, &output, 5U, true, 1234U, line, sizeof(line)));
    CHECK(strncmp(line, "C_KEY_CSV,1234,5,5,1,3,1430,860,1250,1,",
                  strlen("C_KEY_CSV,1234,5,5,1,3,1430,860,1250,1,")) == 0);
    CHECK(strstr(line, ",0.250,1.800,1.500,8.00,0.030,WELCOME,2") != NULL);
    CHECK(comma_count(line) == 16U);
}

static void test_no_pose_line(void)
{
    const c_key_pipeline_output_t output = {
        .tag_id = 5U,
        .state = C_KEY_STATE_NO_KEY,
        .events = C_KEY_EVENT_STATE_CHANGED,
    };
    char line[C_KEY_TELEMETRY_LINE_LENGTH];

    CHECK(c_key_telemetry_format(
        NULL, &output, 5U, false, 2000U, line, sizeof(line)));
    CHECK(strstr(line, ",0,,,,,,NO_KEY,1") != NULL);
    CHECK(comma_count(line) == 16U);
    CHECK(!c_key_telemetry_format(
        NULL, NULL, 5U, false, 0U, line, sizeof(line)));
    CHECK(!c_key_telemetry_format(
        NULL, &output, 16U, false, 0U, line, sizeof(line)));
}

int run_telemetry_tests(void)
{
    test_pose_line();
    test_no_pose_line();
    return failures;
}
