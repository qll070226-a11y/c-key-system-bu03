#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bu04_pdoa.h"
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

#ifdef CONFIG_BU04_ENABLE_RAW_CAPTURE
#define BU04_RAW_CAPTURE_ENABLED 1
#else
#define BU04_RAW_CAPTURE_ENABLED 0
#endif

static const char *TAG = "C_KEY";
static uint8_t s_raw_frame[CONFIG_BU04_FRAME_BUFFER_SIZE];
static size_t s_raw_frame_len;
static uint32_t s_raw_frame_count;
static uint32_t s_total_bytes;
static uint32_t s_overflow_count;
static uint32_t s_last_valid_frame_ms;
static uint32_t s_last_status_log_ms;
static uint8_t s_accepted_id = CONFIG_C_KEY_ACCEPTED_ID_DEFAULT;
static bool s_link_active;
static bu04_pdoa_stream_t s_pdoa_stream;
static c_key_pipeline_t s_pipeline;
static c_key_io_t s_io;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint8_t logical_tag_id(uint16_t address)
{
    if (address == (uint16_t)CONFIG_C_KEY_PDOA_TAG_ADDRESS) {
        return (uint8_t)CONFIG_C_KEY_LOGICAL_TAG_ID;
    }
    return UINT8_MAX;
}

static c_key_pipeline_config_t make_pipeline_config(void)
{
    return (c_key_pipeline_config_t){
        .anchor_positions = {
            {-0.22f, 0.0f},
            {0.22f, 0.0f},
        },
        .distance_scale_factors = {
            (float)CONFIG_C_KEY_PDOA_DISTANCE_SCALE_PPM * 0.000001f,
            1.0f,
        },
        .distance_offsets_m = {
            (float)CONFIG_C_KEY_PDOA_DISTANCE_OFFSET_MM * 0.001f,
            0.0f,
        },
        .door_center = {0.0f, 0.0f},
        .door_radius_m = (float)CONFIG_C_KEY_DOOR_RADIUS_MM * 0.001f,
        .front_angle_offset_deg = (float)CONFIG_C_KEY_FRONT_OFFSET_DEG,
        .angle_calibration_measured_deg = {
            (float)CONFIG_C_KEY_ANGLE_CAL_RAW_NEG45_TENTHS * 0.1f,
            (float)CONFIG_C_KEY_ANGLE_CAL_RAW_NEG30_TENTHS * 0.1f,
            (float)CONFIG_C_KEY_ANGLE_CAL_RAW_NEG15_TENTHS * 0.1f,
            0.0f, 15.0f, 30.0f, 45.0f,
        },
        .angle_calibration_reference_deg = {
            -45.0f, -30.0f, -15.0f, 0.0f, 15.0f, 30.0f, 45.0f,
        },
        .filter_alpha = (float)CONFIG_C_KEY_FILTER_ALPHA_PERCENT * 0.01f,
        .angle_one_euro_min_cutoff_hz =
            (float)CONFIG_C_KEY_ANGLE_ONE_EURO_MIN_CUTOFF_CENTIHZ * 0.01f,
        .angle_one_euro_beta =
            (float)CONFIG_C_KEY_ANGLE_ONE_EURO_BETA_MILLI * 0.001f,
        .angle_one_euro_derivative_cutoff_hz =
            (float)CONFIG_C_KEY_ANGLE_ONE_EURO_DERIVATIVE_CUTOFF_CENTIHZ * 0.01f,
        .angle_hampel_sigma =
            (float)CONFIG_C_KEY_ANGLE_HAMPEL_SIGMA_TENTHS * 0.1f,
        .angle_hampel_min_threshold_deg =
            (float)CONFIG_C_KEY_ANGLE_HAMPEL_MIN_THRESHOLD_DEG,
        .angle_hampel_max_rejections =
            CONFIG_C_KEY_ANGLE_HAMPEL_MAX_REJECTIONS,
        .minimum_distance_m = 0.05f,
        .maximum_distance_m = 20.0f,
        .maximum_residual_m = 0.35f,
        .maximum_age_ms = CONFIG_BU04_SIGNAL_TIMEOUT_MS,
        .maximum_skew_ms = 0U,
        .accepted_id = s_accepted_id,
        .thresholds = c_key_default_thresholds(),
    };
}

static void dump_raw_frame(const char *reason)
{
    if (s_raw_frame_len == 0U) {
        return;
    }
    ++s_raw_frame_count;
    ESP_LOGI(TAG,
             "raw frame=%" PRIu32 " len=%u reason=%s",
             s_raw_frame_count,
             (unsigned)s_raw_frame_len,
             reason);
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
}

static void log_pipeline_output(const c_key_pipeline_output_t *output,
                                const bu04_pdoa_frame_t *frame,
                                uint32_t timestamp_ms,
                                bool force)
{
    char diagnostic[C_KEY_DIAGNOSTIC_LINE_LENGTH];
    if (c_key_diagnostic_format(frame,
                                output,
                                s_accepted_id,
                                s_link_active,
                                timestamp_ms,
                                s_pdoa_stream.accepted_frames,
                                s_pdoa_stream.rejected_frames,
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
                 "PDOA ok=%" PRIu32 " bad=%" PRIu32
                 " addr=%04X D=%" PRIu32 "cm A=%" PRId32
                 "deg fp=%" PRId32 " rx=%" PRId32,
                 s_pdoa_stream.accepted_frames,
                 s_pdoa_stream.rejected_frames,
                 (unsigned)frame->tag_address,
                 frame->distance_cm,
                 frame->angle_deg,
                 frame->first_path_power,
                 frame->rx_level);
    }

    c_key_display_frame_t display;
    if (c_key_display_format(output, s_accepted_id, s_link_active, &display)) {
        if (c_key_tft_ready()) {
            const esp_err_t result = c_key_tft_render(&display);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "TFT render failed: %s", esp_err_to_name(result));
            }
        }
        for (size_t i = 0; i < C_KEY_DISPLAY_LINE_COUNT; ++i) {
            ESP_LOGI(TAG, "%s", display.lines[i]);
        }
    }
}

static void apply_pipeline_input(const c_key_pdoa_input_t *input,
                                 const bu04_pdoa_frame_t *frame,
                                 bool force_log)
{
    c_key_pipeline_output_t output;
    if (!c_key_pipeline_process_pdoa(&s_pipeline, input, &output)) {
        c_key_io_force_safe(&s_io);
        ESP_LOGE(TAG, "PDOA pipeline rejected input; outputs forced safe");
        return;
    }
    c_key_io_apply_state(&s_io, output.state, output.events, input->now_ms);
    log_pipeline_output(&output, frame, input->now_ms, force_log);
}

static void handle_uart_byte(uint8_t value, uint32_t timestamp_ms)
{
    bu04_pdoa_frame_t frame;
    const bu04_pdoa_feed_result_t result =
        bu04_pdoa_stream_feed(&s_pdoa_stream, value, &frame);
    if (result != BU04_PDOA_FEED_FRAME) {
        return;
    }

    const bool measurement_valid =
        frame.distance_cm > 0U &&
        frame.distance_cm <= 2000U &&
        frame.angle_deg >= -180 &&
        frame.angle_deg <= 180;
    const c_key_pdoa_input_t input = {
        .signal_present = true,
        .measurement_valid = measurement_valid,
        .tag_id = logical_tag_id(frame.tag_address),
        .now_ms = timestamp_ms,
        .sequence = frame.sequence,
        .distance_m = (float)frame.distance_cm * 0.01f,
        .angle_deg = (float)frame.angle_deg,
    };

    const bool link_was_active = s_link_active;
    s_link_active = true;
    s_last_valid_frame_ms = timestamp_ms;
    apply_pipeline_input(&input, &frame, link_was_active != s_link_active);
}

static void handle_link_timeout(uint32_t timestamp_ms)
{
    if (!s_link_active ||
        timestamp_ms - s_last_valid_frame_ms < CONFIG_BU04_SIGNAL_TIMEOUT_MS) {
        return;
    }

    s_link_active = false;
    const c_key_pdoa_input_t input = {
        .signal_present = false,
        .tag_id = UINT8_MAX,
        .now_ms = timestamp_ms,
    };
    apply_pipeline_input(&input, NULL, true);
    ESP_LOGW(TAG,
             "PDOA timeout after %d ms; lock forced closed",
             CONFIG_BU04_SIGNAL_TIMEOUT_MS);
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
        ESP_ERROR_CHECK(c_key_pipeline_set_accepted_id(
                            &s_pipeline, accepted_id)
                            ? ESP_OK
                            : ESP_ERR_INVALID_STATE);
    }
    if (changed) {
        ESP_LOGI(TAG, "accepted logical ID changed to %u", s_accepted_id);
    }
}

static void init_bu04_uart(void)
{
    const uart_port_t port = (uart_port_t)CONFIG_BU04_UART_NUM;
    const uart_config_t config = {
        .baud_rate = CONFIG_BU04_UART_BAUD_RATE,
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
                                 CONFIG_BU04_UART_TX_GPIO,
                                 CONFIG_BU04_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_flush_input(port));
    ESP_LOGI(TAG,
             "UART%d %d baud RX-only: GPIO%d <- BU04 UART2_TX",
             CONFIG_BU04_UART_NUM,
             CONFIG_BU04_UART_BAUD_RATE,
             CONFIG_BU04_UART_RX_GPIO);
}

void app_main(void)
{
    uint8_t read_buffer[READ_BUFFER_SIZE];
    uint32_t last_byte_ms = now_ms();
    uint32_t last_stats_ms = last_byte_ms;

    bu04_pdoa_stream_init(&s_pdoa_stream);
    const c_key_pipeline_config_t pipeline_config = make_pipeline_config();
    ESP_ERROR_CHECK(c_key_pipeline_init(&s_pipeline, &pipeline_config)
                        ? ESP_OK
                        : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(c_key_io_init(&s_io));
    const esp_err_t tft_result = c_key_tft_init();
    if (tft_result != ESP_OK && tft_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG,
                 "TFT init failed; lock logic continues: %s",
                 esp_err_to_name(tft_result));
    }
    init_bu04_uart();

    ESP_LOGI(TAG,
             "PDOA door ready: tag=%04X logical ID=%u accepted ID=%u raw=%d",
             (unsigned)CONFIG_C_KEY_PDOA_TAG_ADDRESS,
             CONFIG_C_KEY_LOGICAL_TAG_ID,
             s_accepted_id,
             BU04_RAW_CAPTURE_ENABLED);
    ESP_LOGI(TAG,
             "PDOA calibration: distance=%dppm/%dmm angle_offset=%ddeg",
             CONFIG_C_KEY_PDOA_DISTANCE_SCALE_PPM,
             CONFIG_C_KEY_PDOA_DISTANCE_OFFSET_MM,
             CONFIG_C_KEY_FRONT_OFFSET_DEG);

    while (true) {
        const int length = uart_read_bytes((uart_port_t)CONFIG_BU04_UART_NUM,
                                           read_buffer,
                                           sizeof(read_buffer),
                                           pdMS_TO_TICKS(20));
        const uint32_t current_ms = now_ms();
        if (length > 0) {
            last_byte_ms = current_ms;
            s_total_bytes += (uint32_t)length;
            for (int i = 0; i < length; ++i) {
                handle_uart_byte(read_buffer[i], current_ms);
                if (BU04_RAW_CAPTURE_ENABLED) {
                    append_raw_byte(read_buffer[i]);
                }
            }
        } else if (BU04_RAW_CAPTURE_ENABLED &&
                   s_raw_frame_len > 0U &&
                   current_ms - last_byte_ms >= CONFIG_BU04_IDLE_FLUSH_MS) {
            dump_raw_frame("idle-timeout");
        }

        poll_dip_switch(current_ms);
        handle_link_timeout(current_ms);

        if (current_ms - last_stats_ms >= STATS_LOG_INTERVAL_MS) {
            ESP_LOGI(TAG,
                     "stats bytes=%" PRIu32 " pdoa_ok=%" PRIu32
                     " pdoa_bad=%" PRIu32 " raw_frames=%" PRIu32
                     " overflows=%" PRIu32,
                     s_total_bytes,
                     s_pdoa_stream.accepted_frames,
                     s_pdoa_stream.rejected_frames,
                     s_raw_frame_count,
                     s_overflow_count);
            last_stats_ms = current_ms;
        }
    }
}
