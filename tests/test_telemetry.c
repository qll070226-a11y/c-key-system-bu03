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
    while (*text != 0) {
        if (*text++ == ',') {
            ++count;
        }
    }
    return count;
}

static void test_pose_line(void)
{
    const bu04_pdoa_frame_t frame = {
        .sequence = 42U,
        .tag_address = 0x6e19U,
        .angle_deg = -21,
        .distance_cm = 157U,
        .first_path_power = -8123,
        .rx_level = -7544,
    };
    const c_key_pipeline_output_t output = {
        .measurement_ready = true,
        .pose_valid = true,
        .tag_id = 0U,
        .corrected_distances_m = {1.55f, 0.0f},
        .filtered_distances_m = {1.52f, 0.0f},
        .pose = {
            .position = {.x_m = -0.55f, .y_m = 1.42f},
            .boundary_distance_m = 1.22f,
            .angle_deg = -20.5f,
        },
        .state = C_KEY_STATE_WELCOME,
        .angle_sample_rejected = true,
        .angle_rejected_samples = 9U,
        .events = C_KEY_EVENT_WELCOME_ON,
    };
    char line[C_KEY_TELEMETRY_LINE_LENGTH];

    CHECK(c_key_telemetry_format(
        &frame, &output, 0U, true, 1234U, line, sizeof(line)));
    CHECK(strncmp(line,
                  "C_KEY_PDOA_CSV,1234,28185,0,0,1,157,-21,1,",
                  strlen("C_KEY_PDOA_CSV,1234,28185,0,0,1,157,-21,1,")) == 0);

    char diagnostic[C_KEY_DIAGNOSTIC_LINE_LENGTH];
    CHECK(c_key_diagnostic_format(&frame,
                                  &output,
                                  0U,
                                  true,
                                  1234U,
                                  77U,
                                  3U,
                                  diagnostic,
                                  sizeof(diagnostic)));
    CHECK(strncmp(
        diagnostic,
        "C_KEY_DIAG_V3,1234,42,28185,0,0,1,1,1,157,-21,-8123,-7544,1550.0,1520.0,-20.50,",
        strlen(
            "C_KEY_DIAG_V3,1234,42,28185,0,0,1,1,1,157,-21,-8123,-7544,1550.0,1520.0,-20.50,")) == 0);
    CHECK(strstr(
        diagnostic,
        "-0.550,1.420,1.220,WELCOME,2,77,3,1,9") != NULL);
    CHECK(comma_count(diagnostic) == 24U);
}

static void test_no_pose_line(void)
{
    const c_key_pipeline_output_t output = {
        .tag_id = 0U,
        .state = C_KEY_STATE_NO_KEY,
        .events = C_KEY_EVENT_STATE_CHANGED,
    };
    char line[C_KEY_DIAGNOSTIC_LINE_LENGTH];

    CHECK(c_key_diagnostic_format(NULL,
                                  &output,
                                  0U,
                                  false,
                                  2000U,
                                  12U,
                                  2U,
                                  line,
                                  sizeof(line)));
    CHECK(strncmp(line, "C_KEY_DIAG_V3,2000,", 19U) == 0);
    CHECK(strstr(line, "NO_KEY,1,12,2,0,0") != NULL);
    CHECK(comma_count(line) == 24U);
    CHECK(!c_key_diagnostic_format(
        NULL, NULL, 0U, false, 0U, 0U, 0U, line, sizeof(line)));
}

int run_telemetry_tests(void)
{
    test_pose_line();
    test_no_pose_line();
    return failures;
}
