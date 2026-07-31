#include "c_key_bu03_usb.h"

#include <inttypes.h>

#include "bu03_twr_usb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

#define USB_HOST_TASK_PRIORITY 20
#define USB_CONNECT_TASK_PRIORITY 5
#define USB_TASK_STACK_SIZE 4096
#define USB_TRANSFER_BUFFER_SIZE 512

static const char *TAG = "C_KEY_BU03_USB";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bu03_twr_usb_stream_t s_stream;
static SemaphoreHandle_t s_disconnected;
static bool s_started;
static bool s_device_connected;
static bool s_tag_id_valid;
static uint16_t s_latest_tag_id;
static uint32_t s_last_tag_id_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool handle_rx(const uint8_t *data, size_t data_len, void *arg)
{
    (void)arg;
    const uint32_t timestamp_ms = now_ms();
    uint16_t invalid_tag_id = UINT16_MAX;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < data_len; ++i) {
        bu03_twr_usb_frame_t frame;
        const bu03_twr_usb_feed_result_t result =
            bu03_twr_usb_stream_feed(&s_stream, data[i], &frame);
        if (result != BU03_TWR_USB_FEED_FRAME) {
            continue;
        }

        s_latest_tag_id = frame.tag_id;
        s_last_tag_id_ms = timestamp_ms;
        s_tag_id_valid = frame.valid_mask != 0U && frame.tag_id <= 15U;
        if (frame.tag_id > 15U) {
            invalid_tag_id = frame.tag_id;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (invalid_tag_id != UINT16_MAX) {
        ESP_LOGW(TAG,
                 "received out-of-range Tagid=%" PRIu16,
                 invalid_tag_id);
    }
    return true;
}

static void handle_device_event(const cdc_acm_host_dev_event_data_t *event,
                                void *user_ctx)
{
    (void)user_ctx;
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC error=%d", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "BU03 main USB disconnected");
        portENTER_CRITICAL(&s_lock);
        s_device_connected = false;
        s_tag_id_valid = false;
        portEXIT_CRITICAL(&s_lock);
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            cdc_acm_host_close(event->data.cdc_hdl));
        xSemaphoreGive(s_disconnected);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        break;
    default:
        ESP_LOGW(TAG, "unsupported CDC event=%d", event->type);
        break;
    }
}

static void usb_library_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t event_flags = 0U;
        const esp_err_t result =
            usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "USB host event error: %s", esp_err_to_name(result));
            continue;
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0U) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(usb_host_device_free_all());
        }
    }
}

static void usb_connection_task(void *arg)
{
    (void)arg;
    const cdc_acm_host_device_config_t device_config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = USB_TRANSFER_BUFFER_SIZE,
        .in_buffer_size = USB_TRANSFER_BUFFER_SIZE,
        .user_arg = NULL,
        .event_cb = handle_device_event,
        .data_cb = handle_rx,
    };

    while (true) {
        portENTER_CRITICAL(&s_lock);
        bu03_twr_usb_stream_init(&s_stream);
        s_device_connected = false;
        s_tag_id_valid = false;
        portEXIT_CRITICAL(&s_lock);
        cdc_acm_dev_hdl_t device = NULL;
        const esp_err_t result = cdc_acm_host_open(
            CONFIG_C_KEY_BU03_USB_VID,
            CONFIG_C_KEY_BU03_USB_PID,
            0,
            &device_config,
            &device);
        if (result != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        portENTER_CRITICAL(&s_lock);
        s_device_connected = true;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG,
                 "BU03 main USB connected VID=%04X PID=%04X",
                 CONFIG_C_KEY_BU03_USB_VID,
                 CONFIG_C_KEY_BU03_USB_PID);
        xSemaphoreTake(s_disconnected, portMAX_DELAY);
    }
}

esp_err_t c_key_bu03_usb_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_disconnected = xSemaphoreCreateBinary();
    if (s_disconnected == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        return result;
    }
    result = cdc_acm_host_install(NULL);
    if (result != ESP_OK) {
        return result;
    }
    if (xTaskCreate(usb_library_task,
                    "bu03_usb_lib",
                    USB_TASK_STACK_SIZE,
                    NULL,
                    USB_HOST_TASK_PRIORITY,
                    NULL) != pdPASS ||
        xTaskCreate(usb_connection_task,
                    "bu03_usb_conn",
                    USB_TASK_STACK_SIZE,
                    NULL,
                    USB_CONNECT_TASK_PRIORITY,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "USB identity receiver started");
    return ESP_OK;
}

bool c_key_bu03_usb_get_recent_tag_id(uint32_t current_ms,
                                     uint32_t maximum_age_ms,
                                     uint8_t *tag_id)
{
    if (tag_id == NULL || maximum_age_ms == 0U) {
        return false;
    }
    bool valid;
    uint16_t latest_tag_id;
    uint32_t last_tag_id_ms;
    portENTER_CRITICAL(&s_lock);
    valid = s_device_connected && s_tag_id_valid;
    latest_tag_id = s_latest_tag_id;
    last_tag_id_ms = s_last_tag_id_ms;
    portEXIT_CRITICAL(&s_lock);

    if (!valid || latest_tag_id > 15U ||
        current_ms - last_tag_id_ms > maximum_age_ms) {
        return false;
    }
    *tag_id = (uint8_t)latest_tag_id;
    return true;
}

void c_key_bu03_usb_get_stats(c_key_bu03_usb_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    stats->device_connected = s_device_connected;
    stats->tag_id_valid = s_tag_id_valid;
    stats->latest_tag_id = s_latest_tag_id;
    stats->last_tag_id_ms = s_last_tag_id_ms;
    stats->accepted_frames = s_stream.accepted_frames;
    stats->rejected_frames = s_stream.rejected_frames;
    portEXIT_CRITICAL(&s_lock);
}
