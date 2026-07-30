#ifndef BU03_UART2_H
#define BU03_UART2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BU03_UART2_ANCHOR_COUNT 8U
#define BU03_UART2_FRAME_SIZE 37U
#define BU03_UART2_LENGTH_VALUE 37U
#define BU03_UART2_HEADER 0xAAU
#define BU03_UART2_FOOTER 0x55U

typedef enum {
    BU03_UART2_ERROR_NONE = 0,
    BU03_UART2_ERROR_ARGUMENT,
    BU03_UART2_ERROR_SIZE,
    BU03_UART2_ERROR_HEADER,
    BU03_UART2_ERROR_LENGTH,
    BU03_UART2_ERROR_FOOTER,
    BU03_UART2_ERROR_CHECKSUM,
} bu03_uart2_error_t;

typedef struct {
    uint8_t version;
    uint8_t valid_mask;
    uint32_t distance_mm[BU03_UART2_ANCHOR_COUNT];
} bu03_uart2_frame_t;

typedef enum {
    BU03_UART2_FEED_NONE = 0,
    BU03_UART2_FEED_FRAME,
    BU03_UART2_FEED_BAD_FRAME,
} bu03_uart2_feed_result_t;

typedef struct {
    uint8_t buffer[BU03_UART2_FRAME_SIZE];
    size_t used;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    bu03_uart2_error_t last_error;
} bu03_uart2_stream_t;

uint8_t bu03_uart2_checksum(const uint8_t *frame);

bool bu03_uart2_decode(const uint8_t *data,
                       size_t length,
                       bu03_uart2_frame_t *frame,
                       bu03_uart2_error_t *error);

void bu03_uart2_stream_init(bu03_uart2_stream_t *stream);

bu03_uart2_feed_result_t bu03_uart2_stream_feed(bu03_uart2_stream_t *stream,
                                                uint8_t byte,
                                                bu03_uart2_frame_t *frame);

#endif
