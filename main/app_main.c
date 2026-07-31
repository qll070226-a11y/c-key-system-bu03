#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bu03_uart2.h"
#include "c_key_bu03_bridge.h"
#include "c_key_bu03_usb.h"
#include "c_key_display.h"
#include "c_key_io.h"
#include "c_key_pipeline.h"
#include "c_key_telemetry.h"
#include "c_key_tft.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define READ_BUFFER_SIZE 256
#define UART_RX_BUFFER_SIZE 4096
#define STATUS_LOG_INTERVAL_MS 1000U
#define STATS_LOG_INTERVAL_MS 5000U

#ifdef CONFIG_BU03_ENABLE_RAW_CAPTURE
#define BU03_RAW_CAPTURE_ENABLED 1
#else
#define BU03_RAW_CAPTURE_ENABLED 0
#endif

static const char *TAG = "C_KEY";
static uint8_t s_raw_frame[CONFIG_BU03_FRAME_BUFFER_SIZE];
static size_t s_raw_frame_len;
static uint32_t s_raw_frame_count;
static uint32_t s_total_bytes;
static uint32_t s_overflow_count;
static uint32_t s_last_valid_frame_ms;
static uint32_t s_last_status_log_ms;
static uint8_t s_accepted_id = CONFIG_BU03_ACCEPTED_ID_DEFAULT;
static bool s_link_active;
static bu03_uart2_stream_t s_uart2_stream;
static c_key_pipeline_t s_pipeline;
static c_key_io_t s_io;
#ifdef CONFIG_C_KEY_ALLOW_ID_FALLBACK
static bool s_identity_fallback_active;
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool resolve_tag_identity(uint32_t timestamp_ms, uint8_t *tag_id)
{
    bool identity_valid = c_key_bu03_usb_get_recent_tag_id(
        timestamp_ms,
        CONFIG_C_KEY_BU03_ID_TIMEOUT_MS,
        tag_id);

#ifdef CONFIG_C_KEY_ALLOW_ID_FALLBACK
    if (!identity_valid) {
        *tag_id = s_accepted_id;
        identity_valid = true;
        if (!s_identity_fallback_active) {
            ESP_LOGW(TAG,
                     "real Tag ID unavailable; using accepted ID %u fallback",
                     s_accepted_id);
        }
        s_identity_fallback_active = true;
    } else if (s_identity_fallback_active) {
        ESP_LOGI(TAG, "real Tag ID restored; fallback disabled");
        s_identity_fallback_active = false;
    }
#endif

    return identity_valid;
}

static c_key_pipeline_config_t make_pipeline_config(void)
{
    c_key_pipeline_config_t config = {
        .anchor_positions = {
            {(float)CONFIG_C_KEY_ANCHOR0_X_MM * 0.001f,
             (float)CONFIG_C_KEY_ANCHOR0_Y_MM * 0.001f},
            {(float)CONFIG_C_KEY_ANCHOR1_X_MM * 0.001f,
             (float)CONFIG_C_KEY_ANCHOR1_Y_MM * 0.001f},
        },
        .distance_scale_factors = {
            (float)CONFIG_C_KEY_ANCHOR0_SCALE_PPM * 0.000001f,
            (float)CONFIG_C_KEY_ANCHOR1_SCALE_PPM * 0.000001f,
        },
        .distance_offsets_m = {
            (float)CONFIG_C_KEY_ANCHOR0_OFFSET_MM * 0.001f,
            (float)CONFIG_C_KEY_ANCHOR1_OFFSET_MM * 0.001f,
        },
        .door_center = {0.0f, 0.0f},
        .door_radius_m = (float)CONFIG_C_KEY_DOOR_RADIUS_MM * 0.001f,
        .front_angle_offset_deg = (float)CONFIG_C_KEY_FRONT_OFFSET_DEG,
        .filter_alpha = (float)CONFIG_C_KEY_FILTER_ALPHA_PERCENT * 0.01f,
        .angle_filter_stationary_alpha =
            (float)CONFIG_C_KEY_ANGLE_FILTER_STATIONARY_ALPHA_PERCENT * 0.01f,
        .angle_filter_moving_alpha =
            (float)CONFIG_C_KEY_ANGLE_FILTER_MOVING_ALPHA_PERCENT * 0.01f,
        .angle_filter_motion_threshold_deg =
            (float)CONFIG_C_KEY_ANGLE_FILTER_MOTION_THRESHOLD_DEG,
        .minimum_distance_m = 0.10f,
        .maximum_distance_m = 20.0f,
        .maximum_residual_m = (float)CONFIG_C_KEY_MAX_RESIDUAL_MM * 0.001f,
        .maximum_age_ms = CONFIG_BU03_SIGNAL_TIMEOUT_MS,
        .maximum_skew_ms = 0U,
        .accepted_id = s_accepted_id,
        .thresholds = c_key_default_thresholds(),
    };
    return config;
}

static bool raw_frame_is_text(const uint8_t *data, size_t length)
{
    if (length == 0U) {
        return false;
    }
    size_t printable = 0U;
    for (size_t i = 0; i < length; ++i) {
        if (isprint((unsigned char)data[i]) || data[i] == '\r' ||
            data[i] == '\n' || data[i] == '\t') {
            ++printable;
        }
    }
    return printable * 100U / length >= 90U;
}

static void dump_raw_frame(const char *reason)
{
    if (s_raw_frame_len == 0U) {
        return;
    }
    ++s_raw_frame_count;
    ESP_LOGI(TAG, "raw frame=%" PRIu32 " len=%u reason=%s",
             s_raw_frame_count, (unsigned)s_raw_frame_len, reason);
    if (raw_frame_is_text(s_raw_frame, s_raw_frame_len)) {
        size_t text_len = s_raw_frame_len;
        while (text_len > 0U &&
               (s_raw_frame[text_len - 1U] == '\r' ||
                s_raw_frame[text_len - 1U] == '\n')) {
            --text_len;
        }
        ESP_LOGI(TAG, "ASCII: %.*s", (int)text_len, (const char *)s_raw_frame);
    }
    ESP_LOG_BUFFER_HEXDUMP(TAG, s_raw_frame, s_raw_frame_len, ESP_LOG_INFO);
    s_raw_frame_len = 0U;
}

static void append_raw_byte(uint8_t value)
{
    if (s_raw_frame_len >= sizeof(s_raw_frame)) {
        ++s_overflow_count;
        dump_raw_frame("buffer-full");
    }
    s_raw_frame[s_raw_frame_len++] = value;
    if (value == '\n') {
        dump_raw_frame("newline");
    }
}

static void log_pipeline_output(const c_key_pipeline_output_t *output,
                                const bu03_uart2_frame_t *frame,
                                uint32_t timestamp_ms,
                                bool force)
{
    if (output == NULL) {
        return;
    }

    char diagnostic[C_KEY_DIAGNOSTIC_LINE_LENGTH];
    if (c_key_diagnostic_format(frame,
                                output,
                                s_accepted_id,
                                s_link_active,
                                timestamp_ms,
                                s_uart2_stream.accepted_frames,
                                s_uart2_stream.rejected_frames,
                                diagnostic,
                                sizeof(diagnostic))) {
        ESP_LOGI("C_KEY_DIAG", "%s", diagnostic);
    }

    if (!force && output->events == C_KEY_EVENT_NONE &&
        timestamp_ms - s_last_status_log_ms < STATUS_LOG_INTERVAL_MS) {
        return;
    }
    s_last_status_log_ms = timestamp_ms;

    char telemetry[C_KEY_TELEMETRY_LINE_LENGTH];
    if (c_key_telemetry_format(frame,
                               output,
                               s_accepted_id,
                               s_link_active,
                               timestamp_ms,
                               telemetry,
                               sizeof(telemetry))) {
        ESP_LOGI("C_KEY_DATA", "%s", telemetry);
    }

    if (frame != NULL) {
        ESP_LOGI(TAG,
                 "UWB ok=%" PRIu32 " bad=%" PRIu32
                 " mask=0x%02X A0=%" PRIu32 " A1=%" PRIu32 " mm",
                 s_uart2_stream.accepted_frames,
                 s_uart2_stream.rejected_frames,
                 frame->valid_mask,
                 frame->distance_mm[0],
                 frame->distance_mm[1]);
    }

    c_key_display_frame_t display;
    if (c_key_display_format(output, s_accepted_id, s_link_active, &display)) {
        if (c_key_tft_ready()) {
            const esp_err_t tft_result = c_key_tft_render(&display);
            if (tft_result != ESP_OK) {
                ESP_LOGW(TAG,
                         "TFT render failed: %s",
                         esp_err_to_name(tft_result));
            }
        }
        for (size_t i = 0; i < C_KEY_DISPLAY_LINE_COUNT; ++i) {
            ESP_LOGI(TAG, "%s", display.lines[i]);
        }
    }
}

static void apply_pipeline_input(const c_key_pipeline_input_t *input,
                                 const bu03_uart2_frame_t *frame,
                                 bool force_log)
{
    c_key_pipeline_output_t output;
    if (!c_key_pipeline_process(&s_pipeline, input, &output)) {
        c_key_io_force_safe(&s_io);
        ESP_LOGE(TAG, "pipeline rejected input; outputs forced safe");
        return;
    }
    c_key_io_apply_state(&s_io, output.state, output.events, input->now_ms);
    log_pipeline_output(&output, frame, input->now_ms, force_log);
}

static void handle_uart2_byte(uint8_t value, uint32_t timestamp_ms)
{
    bu03_uart2_frame_t frame;
    const bu03_uart2_feed_result_t result =
        bu03_uart2_stream_feed(&s_uart2_stream, value, &frame);
    if (result != BU03_UART2_FEED_FRAME) {
        return;
    }

    uint8_t received_tag_id = 0U;
    const bool identity_valid =
        resolve_tag_identity(timestamp_ms, &received_tag_id);

    c_key_pipeline_input_t input;
    if (!c_key_input_from_bu03_uart2(
            &frame,
            received_tag_id,
            timestamp_ms,
            (uint16_t)s_uart2_stream.accepted_frames,
            &input)) {
        c_key_io_force_safe(&s_io);
        return;
    }
    input.signal_present = input.signal_present && identity_valid;

    const bool link_was_active = s_link_active;
    s_link_active = input.signal_present;
    s_last_valid_frame_ms = timestamp_ms;
    apply_pipeline_input(&input, &frame, link_was_active != s_link_active);
}

static void handle_link_timeout(uint32_t timestamp_ms)
{
    if (!s_link_active ||
        timestamp_ms - s_last_valid_frame_ms < CONFIG_BU03_SIGNAL_TIMEOUT_MS) {
        return;
    }

    s_link_active = false;
    c_key_pipeline_input_t input = {
        .signal_present = false,
        .tag_id = 0U,
        .now_ms = timestamp_ms,
    };
    apply_pipeline_input(&input, NULL, true);
    ESP_LOGW(TAG, "UWB timeout after %d ms; lock forced closed",
             CONFIG_BU03_SIGNAL_TIMEOUT_MS);
}

static void poll_dip_switch(uint32_t timestamp_ms)
{
    uint8_t accepted_id;
    bool changed;
    if (!c_key_io_poll_accepted_id(
            &s_io, timestamp_ms, &accepted_id, &changed)) {
        return;
    }
    if (accepted_id != s_accepted_id) {
        s_accepted_id = accepted_id;
        ESP_ERROR_CHECK(c_key_pipeline_set_accepted_id(&s_pipeline, accepted_id)
                            ? ESP_OK
                            : ESP_ERR_INVALID_STATE);
    }
    if (changed) {
        ESP_LOGI(TAG, "accepted ID changed to %u", s_accepted_id);
    }
}

static void init_bu03_uart(void)
{
    const uart_port_t port = (uart_port_t)CONFIG_BU03_UART_NUM;
    const uart_config_t config = {
        .baud_rate = CONFIG_BU03_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {0},
    };

    ESP_ERROR_CHECK(uart_driver_install(port, UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &config));
    ESP_ERROR_CHECK(uart_set_pin(port,
                                 CONFIG_BU03_UART_TX_GPIO,
                                 CONFIG_BU03_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_flush_input(port));
    if (CONFIG_BU03_UART_TX_GPIO == UART_PIN_NO_CHANGE) {
        ESP_LOGI(TAG,
                 "UART%d %d baud RX-only: GPIO%d <- Anchor0 PA2",
                 CONFIG_BU03_UART_NUM,
                 CONFIG_BU03_UART_BAUD_RATE,
                 CONFIG_BU03_UART_RX_GPIO);
    } else {
        ESP_LOGI(TAG,
                 "UART%d %d baud: TX GPIO%d -> Anchor0 PA3, "
                 "RX GPIO%d <- Anchor0 PA2",
                 CONFIG_BU03_UART_NUM,
                 CONFIG_BU03_UART_BAUD_RATE,
                 CONFIG_BU03_UART_TX_GPIO,
                 CONFIG_BU03_UART_RX_GPIO);
    }
}

void app_main(void)
{
    uint8_t read_buffer[READ_BUFFER_SIZE];
    uint32_t last_byte_ms = now_ms();
    uint32_t last_stats_ms = last_byte_ms;

    bu03_uart2_stream_init(&s_uart2_stream);
    const c_key_pipeline_config_t pipeline_config = make_pipeline_config();
    ESP_ERROR_CHECK(c_key_pipeline_init(&s_pipeline, &pipeline_config)
                        ? ESP_OK
                        : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(c_key_io_init(&s_io));
    const esp_err_t tft_init_result = c_key_tft_init();
    if (tft_init_result != ESP_OK &&
        tft_init_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG,
                 "TFT init failed; lock logic continues: %s",
                 esp_err_to_name(tft_init_result));
    }
    init_bu03_uart();
    const esp_err_t usb_id_result = c_key_bu03_usb_start();
    if (usb_id_result != ESP_OK) {
#ifdef CONFIG_C_KEY_ALLOW_ID_FALLBACK
        ESP_LOGW(TAG,
                 "BU03 USB identity receiver failed: %s; debug fallback enabled",
                 esp_err_to_name(usb_id_result));
#else
        ESP_LOGE(TAG,
                 "BU03 USB identity receiver failed: %s; lock remains closed",
                 esp_err_to_name(usb_id_result));
#endif
    }

#ifdef CONFIG_C_KEY_ALLOW_ID_FALLBACK
    ESP_LOGW(TAG,
             "ID fallback enabled for debugging; disable before acceptance");
#endif

    ESP_LOGI(TAG,
             "door controller ready: accepted ID=%u raw_capture=%d",
             s_accepted_id,
             BU03_RAW_CAPTURE_ENABLED);
    ESP_LOGI(TAG,
             "anchor coordinates mm: A0=(%d,%d) A1=(%d,%d)",
             CONFIG_C_KEY_ANCHOR0_X_MM,
             CONFIG_C_KEY_ANCHOR0_Y_MM,
             CONFIG_C_KEY_ANCHOR1_X_MM,
             CONFIG_C_KEY_ANCHOR1_Y_MM);
    ESP_LOGI(TAG,
             "range calibration: A0=%dppm/%dmm A1=%dppm/%dmm",
             CONFIG_C_KEY_ANCHOR0_SCALE_PPM,
             CONFIG_C_KEY_ANCHOR0_OFFSET_MM,
             CONFIG_C_KEY_ANCHOR1_SCALE_PPM,
             CONFIG_C_KEY_ANCHOR1_OFFSET_MM);

    while (true) {
        const int length = uart_read_bytes((uart_port_t)CONFIG_BU03_UART_NUM,
                                           read_buffer,
                                           sizeof(read_buffer),
                                           pdMS_TO_TICKS(20));
        const uint32_t current_ms = now_ms();

        if (length > 0) {
            last_byte_ms = current_ms;
            s_total_bytes += (uint32_t)length;
            for (int i = 0; i < length; ++i) {
                handle_uart2_byte(read_buffer[i], current_ms);
                if (BU03_RAW_CAPTURE_ENABLED) {
                    append_raw_byte(read_buffer[i]);
                }
            }
        } else if (BU03_RAW_CAPTURE_ENABLED &&
                   s_raw_frame_len > 0U &&
                   current_ms - last_byte_ms >= CONFIG_BU03_IDLE_FLUSH_MS) {
            dump_raw_frame("idle-timeout");
        }

        poll_dip_switch(current_ms);
        handle_link_timeout(current_ms);

        if (current_ms - last_stats_ms >= STATS_LOG_INTERVAL_MS) {
            c_key_bu03_usb_stats_t usb_stats;
            c_key_bu03_usb_get_stats(&usb_stats);
            ESP_LOGI(TAG,
                     "stats bytes=%" PRIu32 " uart2_ok=%" PRIu32
                     " uart2_bad=%" PRIu32 " raw_frames=%" PRIu32
                     " overflows=%" PRIu32 " usb_connected=%d"
                     " usb_id_ok=%d usb_ok=%" PRIu32 " usb_bad=%" PRIu32,
                     s_total_bytes,
                     s_uart2_stream.accepted_frames,
                     s_uart2_stream.rejected_frames,
                     s_raw_frame_count,
                     s_overflow_count,
                     usb_stats.device_connected,
                     usb_stats.tag_id_valid,
                     usb_stats.accepted_frames,
                     usb_stats.rejected_frames);
            last_stats_ms = current_ms;
        }
    }
}
