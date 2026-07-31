#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bu03_twr_usb.h"

static int failures;

static void check_impl(bool condition, const char *text, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL bu03 usb line %d: %s\n", line, text);
        ++failures;
    }
}

#define CHECK(condition) check_impl((condition), #condition, __LINE__)

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void make_frame(uint8_t frame[BU03_TWR_USB_FRAME_SIZE])
{
    memset(frame, 0, BU03_TWR_USB_FRAME_SIZE);
    memcpy(frame, "CmdM:4", BU03_TWR_USB_HEADER_SIZE);
    frame[6] = BU03_TWR_USB_PAYLOAD_SIZE;
    write_u32_le(&frame[7], 123456U);
    write_u16_le(&frame[11], 5U);
    write_u16_le(&frame[13], 0U);
    frame[15] = 42U;
    frame[16] = 0x03U;
    write_u32_le(&frame[17], 1430U);
    write_u32_le(&frame[21], 860U);
    frame[49] = 1U;
    write_u32_le(&frame[50], 1400U);
    write_u32_le(&frame[54], 850U);
    frame[99] = '\r';
    frame[100] = '\n';
    frame[98] = bu03_twr_usb_checksum(frame);
}

static void test_decode(void)
{
    uint8_t raw[BU03_TWR_USB_FRAME_SIZE];
    bu03_twr_usb_frame_t frame;
    bu03_twr_usb_error_t error;
    make_frame(raw);

    CHECK(bu03_twr_usb_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_TWR_USB_ERROR_NONE);
    CHECK(frame.timer == 123456U);
    CHECK(frame.tag_id == 5U);
    CHECK(frame.anchor_id == 0U);
    CHECK(frame.sequence == 42U);
    CHECK(frame.valid_mask == 0x03U);
    CHECK(frame.raw_distance_mm[0] == 1430U);
    CHECK(frame.filtered_distance_mm[1] == 850U);
    CHECK(frame.kalman_enabled);

    raw[98] ^= 1U;
    CHECK(!bu03_twr_usb_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_TWR_USB_ERROR_CHECKSUM);
}

static void test_stream_and_resync(void)
{
    uint8_t raw[BU03_TWR_USB_FRAME_SIZE];
    bu03_twr_usb_frame_t frame;
    bu03_twr_usb_stream_t stream;
    make_frame(raw);
    bu03_twr_usb_stream_init(&stream);

    const uint8_t noise[] = {'C', 'm', 'x', 0x00U};
    for (size_t i = 0; i < sizeof(noise); ++i) {
        (void)bu03_twr_usb_stream_feed(&stream, noise[i], &frame);
    }

    bu03_twr_usb_feed_result_t result = BU03_TWR_USB_FEED_NONE;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        result = bu03_twr_usb_stream_feed(&stream, raw[i], &frame);
    }
    CHECK(result == BU03_TWR_USB_FEED_FRAME);
    CHECK(stream.accepted_frames == 1U);
    CHECK(stream.rejected_frames >= 1U);
    CHECK(frame.tag_id == 5U);
}

static void test_real_hardware_frame(void)
{
    const uint8_t raw[BU03_TWR_USB_FRAME_SIZE] = {
        0x43U, 0x6dU, 0x64U, 0x4dU, 0x3aU, 0x34U, 0x5bU, 0x87U,
        0x85U, 0x22U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x2eU,
        0x03U, 0x4cU, 0x04U, 0x00U, 0x00U, 0x10U, 0x04U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x4cU, 0x04U, 0x00U, 0x00U, 0x10U, 0x04U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x57U, 0x0dU, 0x0aU,
    };
    bu03_twr_usb_frame_t frame;
    bu03_twr_usb_error_t error;

    CHECK(bu03_twr_usb_decode(raw, sizeof(raw), &frame, &error));
    CHECK(error == BU03_TWR_USB_ERROR_NONE);
    CHECK(frame.tag_id == 0U);
    CHECK(frame.anchor_id == 1U);
    CHECK(frame.sequence == 46U);
    CHECK(frame.valid_mask == 0x03U);
    CHECK(frame.raw_distance_mm[0] == 1100U);
    CHECK(frame.raw_distance_mm[1] == 1040U);
    CHECK(frame.filtered_distance_mm[0] == 1100U);
    CHECK(frame.filtered_distance_mm[1] == 1040U);
}

int run_bu03_twr_usb_tests(void)
{
    test_decode();
    test_stream_and_resync();
    test_real_hardware_frame();
    return failures;
}
