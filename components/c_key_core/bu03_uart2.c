#include "bu03_uart2.h"

#include <string.h>

#define BU03_UART2_VERSION_OFFSET 2U
#define BU03_UART2_DATA_OFFSET 3U
#define BU03_UART2_CHECKSUM_OFFSET 35U
#define BU03_UART2_FOOTER_OFFSET 36U

static void set_error(bu03_uart2_error_t *error, bu03_uart2_error_t value)
{
    if (error != NULL) {
        *error = value;
    }
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

uint8_t bu03_uart2_checksum(const uint8_t *frame)
{
    uint8_t sum = 0U;
    if (frame == NULL) {
        return 0U;
    }
    for (size_t i = 0; i < BU03_UART2_CHECKSUM_OFFSET; ++i) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return sum;
}

static void resync_after_bad_frame(bu03_uart2_stream_t *stream)
{
    for (size_t i = 1U; i + 1U < stream->used; ++i) {
        if (stream->buffer[i] == BU03_UART2_HEADER &&
            stream->buffer[i + 1U] == BU03_UART2_LENGTH_VALUE) {
            const size_t remaining = stream->used - i;
            memmove(stream->buffer, &stream->buffer[i], remaining);
            stream->used = remaining;
            return;
        }
    }

    if (stream->used > 0U &&
        stream->buffer[stream->used - 1U] == BU03_UART2_HEADER) {
        stream->buffer[0] = BU03_UART2_HEADER;
        stream->used = 1U;
        return;
    }

    stream->used = 0U;
}

bool bu03_uart2_decode(const uint8_t *data,
                       size_t length,
                       bu03_uart2_frame_t *frame,
                       bu03_uart2_error_t *error)
{
    if (data == NULL || frame == NULL) {
        set_error(error, BU03_UART2_ERROR_ARGUMENT);
        return false;
    }
    if (length != BU03_UART2_FRAME_SIZE) {
        set_error(error, BU03_UART2_ERROR_SIZE);
        return false;
    }
    if (data[0] != BU03_UART2_HEADER) {
        set_error(error, BU03_UART2_ERROR_HEADER);
        return false;
    }
    if (data[1] != BU03_UART2_LENGTH_VALUE) {
        set_error(error, BU03_UART2_ERROR_LENGTH);
        return false;
    }
    if (data[BU03_UART2_FOOTER_OFFSET] != BU03_UART2_FOOTER) {
        set_error(error, BU03_UART2_ERROR_FOOTER);
        return false;
    }
    if (data[BU03_UART2_CHECKSUM_OFFSET] != bu03_uart2_checksum(data)) {
        set_error(error, BU03_UART2_ERROR_CHECKSUM);
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->version = data[BU03_UART2_VERSION_OFFSET];
    for (size_t i = 0; i < BU03_UART2_ANCHOR_COUNT; ++i) {
        const uint32_t distance_mm =
            read_u32_le(&data[BU03_UART2_DATA_OFFSET + i * sizeof(uint32_t)]);
        frame->distance_mm[i] = distance_mm;
        if (distance_mm != 0U && distance_mm != UINT32_MAX) {
            frame->valid_mask |= (uint8_t)(1U << i);
        }
    }
    set_error(error, BU03_UART2_ERROR_NONE);
    return true;
}

void bu03_uart2_stream_init(bu03_uart2_stream_t *stream)
{
    if (stream != NULL) {
        memset(stream, 0, sizeof(*stream));
    }
}

bu03_uart2_feed_result_t bu03_uart2_stream_feed(bu03_uart2_stream_t *stream,
                                                uint8_t byte,
                                                bu03_uart2_frame_t *frame)
{
    if (stream == NULL || frame == NULL) {
        return BU03_UART2_FEED_BAD_FRAME;
    }

    if (stream->used == 0U) {
        if (byte == BU03_UART2_HEADER) {
            stream->buffer[stream->used++] = byte;
        }
        return BU03_UART2_FEED_NONE;
    }

    if (stream->used == 1U && byte != BU03_UART2_LENGTH_VALUE) {
        ++stream->rejected_frames;
        stream->last_error = BU03_UART2_ERROR_LENGTH;
        stream->used = 0U;
        if (byte == BU03_UART2_HEADER) {
            stream->buffer[stream->used++] = byte;
        }
        return BU03_UART2_FEED_BAD_FRAME;
    }

    stream->buffer[stream->used++] = byte;
    if (stream->used < BU03_UART2_FRAME_SIZE) {
        return BU03_UART2_FEED_NONE;
    }

    bu03_uart2_error_t error = BU03_UART2_ERROR_NONE;
    const bool valid =
        bu03_uart2_decode(stream->buffer, stream->used, frame, &error);
    stream->last_error = error;
    if (!valid) {
        ++stream->rejected_frames;
        resync_after_bad_frame(stream);
        return BU03_UART2_FEED_BAD_FRAME;
    }

    stream->used = 0U;
    ++stream->accepted_frames;
    return BU03_UART2_FEED_FRAME;
}
