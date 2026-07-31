#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bu04_pdoa.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL bu04 PDOA line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static const uint8_t REAL_FRAME[BU04_PDOA_FRAME_SIZE] = {
    0x2aU, 0x1bU, 0x0cU, 0x19U, 0x6eU, 0xebU, 0xffU, 0xffU,
    0xffU, 0x39U, 0x00U, 0x00U, 0x00U, 0x00U, 0xc0U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x96U, 0x23U,
};

static void test_real_frame(void)
{
    bu04_pdoa_frame_t frame;
    bu04_pdoa_error_t error;

    CHECK(bu04_pdoa_checksum(REAL_FRAME) == 0x96U);
    CHECK(bu04_pdoa_decode(
        REAL_FRAME, sizeof(REAL_FRAME), &frame, &error));
    CHECK(error == BU04_PDOA_ERROR_NONE);
    CHECK(frame.sequence == 0x0cU);
    CHECK(frame.tag_address == 0x6e19U);
    CHECK(frame.angle_deg == -21);
    CHECK(frame.distance_cm == 57U);
    CHECK(frame.user_command == 0xc000U);
    CHECK(frame.first_path_power == 0);
    CHECK(frame.rx_level == 0);
}

static void test_reject_corruption(void)
{
    uint8_t raw[BU04_PDOA_FRAME_SIZE];
    bu04_pdoa_frame_t frame;
    bu04_pdoa_error_t error;
    memcpy(raw, REAL_FRAME, sizeof(raw));

    raw[9] ^= 0x01U;
    CHECK(!bu04_pdoa_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU04_PDOA_ERROR_CHECKSUM);
    memcpy(raw, REAL_FRAME, sizeof(raw));
    raw[30] = 0U;
    CHECK(!bu04_pdoa_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU04_PDOA_ERROR_FOOTER);
    CHECK(!bu04_pdoa_decode(raw, sizeof(raw) - 1U, &frame, &error));
    CHECK(error == BU04_PDOA_ERROR_SIZE);
}

static void test_stream_resynchronization(void)
{
    bu04_pdoa_stream_t stream;
    bu04_pdoa_frame_t frame;
    bu04_pdoa_stream_init(&stream);

    CHECK(bu04_pdoa_stream_feed(&stream, 0x99U, &frame) ==
          BU04_PDOA_FEED_NONE);
    CHECK(bu04_pdoa_stream_feed(&stream, BU04_PDOA_HEADER, &frame) ==
          BU04_PDOA_FEED_NONE);
    CHECK(bu04_pdoa_stream_feed(&stream, 0x00U, &frame) ==
          BU04_PDOA_FEED_BAD_FRAME);

    bu04_pdoa_feed_result_t result = BU04_PDOA_FEED_NONE;
    for (size_t i = 0; i < sizeof(REAL_FRAME); ++i) {
        result = bu04_pdoa_stream_feed(&stream, REAL_FRAME[i], &frame);
    }
    CHECK(result == BU04_PDOA_FEED_FRAME);
    CHECK(stream.accepted_frames == 1U);
    CHECK(stream.rejected_frames == 1U);
    CHECK(frame.tag_address == 0x6e19U);
}

int run_bu04_pdoa_tests(void)
{
    test_real_frame();
    test_reject_corruption();
    test_stream_resynchronization();
    return failures;
}
