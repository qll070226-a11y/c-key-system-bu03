#ifndef BU04_PDOA_H
#define BU04_PDOA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BU04_PDOA_FRAME_SIZE 31U
#define BU04_PDOA_LENGTH_VALUE 0x1BU
#define BU04_PDOA_HEADER 0x2AU
#define BU04_PDOA_FOOTER 0x23U

typedef enum {
    BU04_PDOA_ERROR_NONE = 0,
    BU04_PDOA_ERROR_ARGUMENT,
    BU04_PDOA_ERROR_SIZE,
    BU04_PDOA_ERROR_HEADER,
    BU04_PDOA_ERROR_LENGTH,
    BU04_PDOA_ERROR_FOOTER,
    BU04_PDOA_ERROR_CHECKSUM,
} bu04_pdoa_error_t;

typedef struct {
    uint8_t sequence;
    uint16_t tag_address;
    int32_t angle_deg;
    uint32_t distance_cm;
    uint16_t user_command;
    int32_t first_path_power;
    int32_t rx_level;
    int16_t acceleration_x;
    int16_t acceleration_y;
    int16_t acceleration_z;
} bu04_pdoa_frame_t;

typedef enum {
    BU04_PDOA_FEED_NONE = 0,
    BU04_PDOA_FEED_FRAME,
    BU04_PDOA_FEED_BAD_FRAME,
} bu04_pdoa_feed_result_t;

typedef struct {
    uint8_t buffer[BU04_PDOA_FRAME_SIZE];
    size_t used;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    bu04_pdoa_error_t last_error;
} bu04_pdoa_stream_t;

uint8_t bu04_pdoa_checksum(const uint8_t *frame);

bool bu04_pdoa_decode(const uint8_t *data,
                      size_t length,
                      bu04_pdoa_frame_t *frame,
                      bu04_pdoa_error_t *error);

void bu04_pdoa_stream_init(bu04_pdoa_stream_t *stream);

bu04_pdoa_feed_result_t bu04_pdoa_stream_feed(bu04_pdoa_stream_t *stream,
                                               uint8_t byte,
                                               bu04_pdoa_frame_t *frame);

#endif
