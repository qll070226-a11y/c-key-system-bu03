#include "bu03_twr_usb.h"

#include <string.h>

static const uint8_t s_header[BU03_TWR_USB_HEADER_SIZE] = {
    'C', 'm', 'd', 'M', ':', '4',
};

enum {
    LENGTH_OFFSET = 6,
    TIMER_OFFSET = 7,
    TAG_ID_OFFSET = 11,
    ANCHOR_ID_OFFSET = 13,
    SEQUENCE_OFFSET = 15,
    MASK_OFFSET = 16,
    RAW_DISTANCE_OFFSET = 17,
    KALMAN_ENABLE_OFFSET = 49,
    FILTERED_DISTANCE_OFFSET = 50,
    CHECKSUM_OFFSET = 98,
    FOOTER_OFFSET = 99,
};

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void set_error(bu03_twr_usb_error_t *error,
                      bu03_twr_usb_error_t value)
{
    if (error != NULL) {
        *error = value;
    }
}

uint8_t bu03_twr_usb_checksum(const uint8_t frame[BU03_TWR_USB_FRAME_SIZE])
{
    if (frame == NULL) {
        return 0U;
    }
    uint8_t checksum = 0U;
    for (size_t i = LENGTH_OFFSET; i < CHECKSUM_OFFSET; ++i) {
        checksum ^= frame[i];
    }
    return checksum;
}

bool bu03_twr_usb_decode(const uint8_t *data,
                         size_t length,
                         bu03_twr_usb_frame_t *frame,
                         bu03_twr_usb_error_t *error)
{
    if (data == NULL || frame == NULL) {
        set_error(error, BU03_TWR_USB_ERROR_ARGUMENT);
        return false;
    }
    if (length != BU03_TWR_USB_FRAME_SIZE) {
        set_error(error, BU03_TWR_USB_ERROR_SIZE);
        return false;
    }
    if (memcmp(data, s_header, sizeof(s_header)) != 0) {
        set_error(error, BU03_TWR_USB_ERROR_HEADER);
        return false;
    }
    if (data[LENGTH_OFFSET] != BU03_TWR_USB_PAYLOAD_SIZE) {
        set_error(error, BU03_TWR_USB_ERROR_LENGTH);
        return false;
    }
    if (data[FOOTER_OFFSET] != '\r' || data[FOOTER_OFFSET + 1U] != '\n') {
        set_error(error, BU03_TWR_USB_ERROR_FOOTER);
        return false;
    }
    if (data[CHECKSUM_OFFSET] != bu03_twr_usb_checksum(data)) {
        set_error(error, BU03_TWR_USB_ERROR_CHECKSUM);
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->timer = read_u32_le(&data[TIMER_OFFSET]);
    frame->tag_id = read_u16_le(&data[TAG_ID_OFFSET]);
    frame->anchor_id = read_u16_le(&data[ANCHOR_ID_OFFSET]);
    frame->sequence = data[SEQUENCE_OFFSET];
    frame->valid_mask = data[MASK_OFFSET];
    for (size_t i = 0; i < 8U; ++i) {
        frame->raw_distance_mm[i] =
            read_u32_le(&data[RAW_DISTANCE_OFFSET + i * sizeof(uint32_t)]);
        frame->filtered_distance_mm[i] =
            read_u32_le(&data[FILTERED_DISTANCE_OFFSET + i * sizeof(uint32_t)]);
    }
    frame->kalman_enabled = data[KALMAN_ENABLE_OFFSET] != 0U;
    set_error(error, BU03_TWR_USB_ERROR_NONE);
    return true;
}

void bu03_twr_usb_stream_init(bu03_twr_usb_stream_t *stream)
{
    if (stream != NULL) {
        memset(stream, 0, sizeof(*stream));
    }
}

static void resynchronize(bu03_twr_usb_stream_t *stream)
{
    size_t keep = 0U;
    const size_t limit = stream->used < BU03_TWR_USB_HEADER_SIZE - 1U
                             ? stream->used
                             : BU03_TWR_USB_HEADER_SIZE - 1U;
    for (size_t candidate = limit; candidate > 0U; --candidate) {
        if (memcmp(&stream->buffer[stream->used - candidate],
                   s_header,
                   candidate) == 0) {
            keep = candidate;
            break;
        }
    }
    if (keep > 0U) {
        memmove(stream->buffer, &stream->buffer[stream->used - keep], keep);
    }
    stream->used = keep;
}

bu03_twr_usb_feed_result_t bu03_twr_usb_stream_feed(
    bu03_twr_usb_stream_t *stream,
    uint8_t byte,
    bu03_twr_usb_frame_t *frame)
{
    if (stream == NULL || frame == NULL) {
        return BU03_TWR_USB_FEED_BAD_FRAME;
    }

    if (stream->used == 0U && byte != s_header[0]) {
        return BU03_TWR_USB_FEED_NONE;
    }
    stream->buffer[stream->used++] = byte;

    if (stream->used <= BU03_TWR_USB_HEADER_SIZE &&
        stream->buffer[stream->used - 1U] != s_header[stream->used - 1U]) {
        ++stream->rejected_frames;
        stream->last_error = BU03_TWR_USB_ERROR_HEADER;
        resynchronize(stream);
        return BU03_TWR_USB_FEED_BAD_FRAME;
    }
    if (stream->used == LENGTH_OFFSET + 1U &&
        stream->buffer[LENGTH_OFFSET] != BU03_TWR_USB_PAYLOAD_SIZE) {
        ++stream->rejected_frames;
        stream->last_error = BU03_TWR_USB_ERROR_LENGTH;
        resynchronize(stream);
        return BU03_TWR_USB_FEED_BAD_FRAME;
    }
    if (stream->used < BU03_TWR_USB_FRAME_SIZE) {
        return BU03_TWR_USB_FEED_NONE;
    }

    bu03_twr_usb_error_t error = BU03_TWR_USB_ERROR_NONE;
    const bool valid = bu03_twr_usb_decode(
        stream->buffer, stream->used, frame, &error);
    if (!valid) {
        ++stream->rejected_frames;
        stream->last_error = error;
        resynchronize(stream);
        return BU03_TWR_USB_FEED_BAD_FRAME;
    }

    ++stream->accepted_frames;
    stream->last_error = BU03_TWR_USB_ERROR_NONE;
    stream->used = 0U;
    return BU03_TWR_USB_FEED_FRAME;
}
