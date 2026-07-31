#ifndef BU03_TWR_USB_H
#define BU03_TWR_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BU03_TWR_USB_HEADER_SIZE 6U
#define BU03_TWR_USB_PAYLOAD_SIZE 91U
#define BU03_TWR_USB_FRAME_SIZE 101U

typedef enum {
    BU03_TWR_USB_ERROR_NONE = 0,
    BU03_TWR_USB_ERROR_ARGUMENT,
    BU03_TWR_USB_ERROR_SIZE,
    BU03_TWR_USB_ERROR_HEADER,
    BU03_TWR_USB_ERROR_LENGTH,
    BU03_TWR_USB_ERROR_FOOTER,
    BU03_TWR_USB_ERROR_CHECKSUM,
} bu03_twr_usb_error_t;

typedef struct {
    uint32_t timer;
    uint16_t tag_id;
    uint16_t anchor_id;
    uint8_t sequence;
    uint8_t valid_mask;
    uint32_t raw_distance_mm[8];
    bool kalman_enabled;
    uint32_t filtered_distance_mm[8];
} bu03_twr_usb_frame_t;

typedef enum {
    BU03_TWR_USB_FEED_NONE = 0,
    BU03_TWR_USB_FEED_FRAME,
    BU03_TWR_USB_FEED_BAD_FRAME,
} bu03_twr_usb_feed_result_t;

typedef struct {
    uint8_t buffer[BU03_TWR_USB_FRAME_SIZE];
    size_t used;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    bu03_twr_usb_error_t last_error;
} bu03_twr_usb_stream_t;

uint8_t bu03_twr_usb_checksum(const uint8_t frame[BU03_TWR_USB_FRAME_SIZE]);

bool bu03_twr_usb_decode(const uint8_t *data,
                         size_t length,
                         bu03_twr_usb_frame_t *frame,
                         bu03_twr_usb_error_t *error);

void bu03_twr_usb_stream_init(bu03_twr_usb_stream_t *stream);

bu03_twr_usb_feed_result_t bu03_twr_usb_stream_feed(
    bu03_twr_usb_stream_t *stream,
    uint8_t byte,
    bu03_twr_usb_frame_t *frame);

#endif
