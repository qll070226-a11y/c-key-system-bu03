#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bu03_uart2.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL bu03 line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void make_frame(uint8_t frame[BU03_UART2_FRAME_SIZE])
{
    memset(frame, 0, BU03_UART2_FRAME_SIZE);
    frame[0] = BU03_UART2_HEADER;
    frame[1] = BU03_UART2_LENGTH_VALUE;
    frame[2] = 1U;
    write_u32_le(&frame[3], 1430U);
    write_u32_le(&frame[7], 860U);
    write_u32_le(&frame[11], 1250U);
    write_u32_le(&frame[15], UINT32_MAX);
    frame[36] = BU03_UART2_FOOTER;
    frame[35] = bu03_uart2_checksum(frame);
}

static void test_decode(void)
{
    uint8_t raw[BU03_UART2_FRAME_SIZE];
    bu03_uart2_frame_t frame;
    bu03_uart2_error_t error;
    make_frame(raw);

    CHECK(bu03_uart2_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_UART2_ERROR_NONE);
    CHECK(frame.version == 1U);
    CHECK(frame.distance_mm[0] == 1430U);
    CHECK(frame.distance_mm[1] == 860U);
    CHECK(frame.distance_mm[2] == 1250U);
    CHECK(frame.valid_mask == 0x07U);

    raw[35] ^= 0x01U;
    CHECK(!bu03_uart2_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_UART2_ERROR_CHECKSUM);
    make_frame(raw);
    raw[36] = 0U;
    CHECK(!bu03_uart2_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_UART2_ERROR_FOOTER);
    CHECK(!bu03_uart2_decode(raw, sizeof(raw) - 1U, &frame, &error));
    CHECK(error == BU03_UART2_ERROR_SIZE);
}

static void test_real_hardware_frame(void)
{
    const uint8_t raw[BU03_UART2_FRAME_SIZE] = {
        0xaaU, 0x25U, 0x01U, 0x9eU, 0x02U, 0x00U, 0x00U, 0x02U,
        0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x75U, 0x55U,
    };
    bu03_uart2_frame_t frame;
    bu03_uart2_error_t error;

    CHECK(bu03_uart2_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_UART2_ERROR_NONE);
    CHECK(frame.version == 1U);
    CHECK(frame.distance_mm[0] == 670U);
    CHECK(frame.distance_mm[1] == 770U);
    CHECK(frame.valid_mask == 0x03U);
}

static void test_stream_resynchronization(void)
{
    uint8_t raw[BU03_UART2_FRAME_SIZE];
    bu03_uart2_frame_t frame;
    bu03_uart2_stream_t stream;
    make_frame(raw);
    bu03_uart2_stream_init(&stream);

    CHECK(bu03_uart2_stream_feed(&stream, 0x12U, &frame) ==
          BU03_UART2_FEED_NONE);
    CHECK(bu03_uart2_stream_feed(&stream, BU03_UART2_HEADER, &frame) ==
          BU03_UART2_FEED_NONE);
    CHECK(bu03_uart2_stream_feed(&stream, 0x00U, &frame) ==
          BU03_UART2_FEED_BAD_FRAME);
    CHECK(stream.rejected_frames == 1U);

    bu03_uart2_feed_result_t result = BU03_UART2_FEED_NONE;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        result = bu03_uart2_stream_feed(&stream, raw[i], &frame);
    }
    CHECK(result == BU03_UART2_FEED_FRAME);
    CHECK(stream.accepted_frames == 1U);
    CHECK(frame.distance_mm[2] == 1250U);
}

static void test_stream_resynchronization_after_noise(void)
{
    uint8_t raw[BU03_UART2_FRAME_SIZE];
    bu03_uart2_frame_t frame;
    bu03_uart2_stream_t stream;
    make_frame(raw);
    bu03_uart2_stream_init(&stream);

    CHECK(bu03_uart2_stream_feed(&stream, BU03_UART2_HEADER, &frame) ==
          BU03_UART2_FEED_NONE);
    CHECK(bu03_uart2_stream_feed(&stream, BU03_UART2_LENGTH_VALUE, &frame) ==
          BU03_UART2_FEED_NONE);
    CHECK(bu03_uart2_stream_feed(&stream, 0x99U, &frame) ==
          BU03_UART2_FEED_NONE);

    bu03_uart2_feed_result_t result = BU03_UART2_FEED_NONE;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        result = bu03_uart2_stream_feed(&stream, raw[i], &frame);
    }

    CHECK(result == BU03_UART2_FEED_FRAME);
    CHECK(stream.rejected_frames == 1U);
    CHECK(stream.accepted_frames == 1U);
    CHECK(frame.distance_mm[0] == 1430U);
}

int run_bu03_uart2_tests(void)
{
    test_decode();
    test_real_hardware_frame();
    test_stream_resynchronization();
    test_stream_resynchronization_after_noise();
    return failures;
}
