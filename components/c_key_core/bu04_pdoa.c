#include "bu04_pdoa.h"

#include <string.h>

#define BU04_PDOA_SEQUENCE_OFFSET 2U
#define BU04_PDOA_ADDRESS_OFFSET 3U
#define BU04_PDOA_ANGLE_OFFSET 5U
#define BU04_PDOA_DISTANCE_OFFSET 9U
#define BU04_PDOA_USER_COMMAND_OFFSET 13U
#define BU04_PDOA_FIRST_PATH_OFFSET 15U
#define BU04_PDOA_RX_LEVEL_OFFSET 19U
#define BU04_PDOA_ACCELERATION_X_OFFSET 23U
#define BU04_PDOA_ACCELERATION_Y_OFFSET 25U
#define BU04_PDOA_ACCELERATION_Z_OFFSET 27U
#define BU04_PDOA_CHECKSUM_OFFSET 29U
#define BU04_PDOA_FOOTER_OFFSET 30U

static void set_error(bu04_pdoa_error_t *error, bu04_pdoa_error_t value)
{
    if (error != NULL) {
        *error = value;
    }
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

uint8_t bu04_pdoa_checksum(const uint8_t *frame)
{
    uint8_t checksum = 0U;
    if (frame == NULL) {
        return 0U;
    }
    for (size_t i = BU04_PDOA_SEQUENCE_OFFSET;
         i < BU04_PDOA_CHECKSUM_OFFSET;
         ++i) {
        checksum ^= frame[i];
    }
    return checksum;
}

static void resync_after_bad_frame(bu04_pdoa_stream_t *stream)
{
    for (size_t i = 1U; i + 1U < stream->used; ++i) {
        if (stream->buffer[i] == BU04_PDOA_HEADER &&
            stream->buffer[i + 1U] == BU04_PDOA_LENGTH_VALUE) {
            const size_t remaining = stream->used - i;
            memmove(stream->buffer, &stream->buffer[i], remaining);
            stream->used = remaining;
            return;
        }
    }

    if (stream->used > 0U &&
        stream->buffer[stream->used - 1U] == BU04_PDOA_HEADER) {
        stream->buffer[0] = BU04_PDOA_HEADER;
        stream->used = 1U;
        return;
    }
    stream->used = 0U;
}

bool bu04_pdoa_decode(const uint8_t *data,
                      size_t length,
                      bu04_pdoa_frame_t *frame,
                      bu04_pdoa_error_t *error)
{
    if (data == NULL || frame == NULL) {
        set_error(error, BU04_PDOA_ERROR_ARGUMENT);
        return false;
    }
    if (length != BU04_PDOA_FRAME_SIZE) {
        set_error(error, BU04_PDOA_ERROR_SIZE);
        return false;
    }
    if (data[0] != BU04_PDOA_HEADER) {
        set_error(error, BU04_PDOA_ERROR_HEADER);
        return false;
    }
    if (data[1] != BU04_PDOA_LENGTH_VALUE) {
        set_error(error, BU04_PDOA_ERROR_LENGTH);
        return false;
    }
    if (data[BU04_PDOA_FOOTER_OFFSET] != BU04_PDOA_FOOTER) {
        set_error(error, BU04_PDOA_ERROR_FOOTER);
        return false;
    }
    if (data[BU04_PDOA_CHECKSUM_OFFSET] != bu04_pdoa_checksum(data)) {
        set_error(error, BU04_PDOA_ERROR_CHECKSUM);
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->sequence = data[BU04_PDOA_SEQUENCE_OFFSET];
    frame->tag_address = read_u16_le(&data[BU04_PDOA_ADDRESS_OFFSET]);
    frame->angle_deg = (int32_t)read_u32_le(&data[BU04_PDOA_ANGLE_OFFSET]);
    frame->distance_cm = read_u32_le(&data[BU04_PDOA_DISTANCE_OFFSET]);
    frame->user_command = read_u16_le(&data[BU04_PDOA_USER_COMMAND_OFFSET]);
    frame->first_path_power =
        (int32_t)read_u32_le(&data[BU04_PDOA_FIRST_PATH_OFFSET]);
    frame->rx_level = (int32_t)read_u32_le(&data[BU04_PDOA_RX_LEVEL_OFFSET]);
    frame->acceleration_x =
        (int16_t)read_u16_le(&data[BU04_PDOA_ACCELERATION_X_OFFSET]);
    frame->acceleration_y =
        (int16_t)read_u16_le(&data[BU04_PDOA_ACCELERATION_Y_OFFSET]);
    frame->acceleration_z =
        (int16_t)read_u16_le(&data[BU04_PDOA_ACCELERATION_Z_OFFSET]);
    set_error(error, BU04_PDOA_ERROR_NONE);
    return true;
}

void bu04_pdoa_stream_init(bu04_pdoa_stream_t *stream)
{
    if (stream != NULL) {
        memset(stream, 0, sizeof(*stream));
    }
}

bu04_pdoa_feed_result_t bu04_pdoa_stream_feed(bu04_pdoa_stream_t *stream,
                                               uint8_t byte,
                                               bu04_pdoa_frame_t *frame)
{
    if (stream == NULL || frame == NULL) {
        return BU04_PDOA_FEED_BAD_FRAME;
    }

    if (stream->used == 0U) {
        if (byte == BU04_PDOA_HEADER) {
            stream->buffer[stream->used++] = byte;
        }
        return BU04_PDOA_FEED_NONE;
    }

    if (stream->used == 1U && byte != BU04_PDOA_LENGTH_VALUE) {
        ++stream->rejected_frames;
        stream->last_error = BU04_PDOA_ERROR_LENGTH;
        stream->used = 0U;
        if (byte == BU04_PDOA_HEADER) {
            stream->buffer[stream->used++] = byte;
        }
        return BU04_PDOA_FEED_BAD_FRAME;
    }

    stream->buffer[stream->used++] = byte;
    if (stream->used < BU04_PDOA_FRAME_SIZE) {
        return BU04_PDOA_FEED_NONE;
    }

    bu04_pdoa_error_t error = BU04_PDOA_ERROR_NONE;
    if (!bu04_pdoa_decode(stream->buffer, stream->used, frame, &error)) {
        ++stream->rejected_frames;
        stream->last_error = error;
        resync_after_bad_frame(stream);
        return BU04_PDOA_FEED_BAD_FRAME;
    }

    stream->used = 0U;
    ++stream->accepted_frames;
    stream->last_error = BU04_PDOA_ERROR_NONE;
    return BU04_PDOA_FEED_FRAME;
}
