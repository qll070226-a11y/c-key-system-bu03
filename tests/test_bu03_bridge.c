#include <stdbool.h>
#include <stdio.h>

#include "c_key_bu03_bridge.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL bridge line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static void test_complete_frame(void)
{
    const bu03_uart2_frame_t frame = {
        .valid_mask = 0x03U,
        .distance_mm = {1430U, 860U},
    };
    c_key_pipeline_input_t input;

    CHECK(c_key_input_from_bu03_uart2(&frame, 5U, 1234U, 77U, &input));
    CHECK(input.signal_present);
    CHECK(input.tag_id == 5U);
    CHECK(input.now_ms == 1234U);
    CHECK(input.anchors[0].valid);
    CHECK(input.anchors[0].distance_m > 1.429f &&
          input.anchors[0].distance_m < 1.431f);
    CHECK(input.anchors[1].sequence == 77U);
}

static void test_partial_and_invalid(void)
{
    const bu03_uart2_frame_t frame = {
        .valid_mask = 0x02U,
        .distance_mm = {0U, 900U, 0U},
    };
    c_key_pipeline_input_t input;

    CHECK(c_key_input_from_bu03_uart2(&frame, 0U, 1U, 2U, &input));
    CHECK(input.signal_present);
    CHECK(!input.anchors[0].valid);
    CHECK(input.anchors[1].valid);
    const bu03_uart2_frame_t unused_anchor_only = {
        .valid_mask = 0x04U,
        .distance_mm = {0U, 0U, 800U},
    };
    CHECK(c_key_input_from_bu03_uart2(
        &unused_anchor_only, 0U, 1U, 3U, &input));
    CHECK(!input.signal_present);
    CHECK(!c_key_input_from_bu03_uart2(NULL, 0U, 0U, 0U, &input));
    CHECK(!c_key_input_from_bu03_uart2(&frame, 16U, 0U, 0U, &input));
    CHECK(!c_key_input_from_bu03_uart2(&frame, 0U, 0U, 0U, NULL));
}

int run_bu03_bridge_tests(void)
{
    test_complete_frame();
    test_partial_and_invalid();
    return failures;
}
