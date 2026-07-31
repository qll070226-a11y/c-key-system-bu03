#include "c_key_tft.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_key_tft_zh_font.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_C_KEY_TFT_ENABLE

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_BAND_HEIGHT 28
#define TFT_GLYPH_WIDTH 5
#define TFT_GLYPH_HEIGHT 7

#define LCD_CMD_SWRESET 0x01U
#define LCD_CMD_RDDID 0x04U
#define LCD_CMD_RDDID4 0xD3U
#define LCD_CMD_SLPOUT 0x11U
#define LCD_CMD_DISPON 0x29U
#define LCD_CMD_CASET 0x2AU
#define LCD_CMD_RASET 0x2BU
#define LCD_CMD_RAMWR 0x2CU
#define LCD_CMD_MADCTL 0x36U
#define LCD_CMD_COLMOD 0x3AU

static const char *TAG = "C_KEY_TFT";
static spi_device_handle_t s_lcd;
static uint8_t *s_band;
static bool s_ready;

static esp_err_t lcd_write_command(uint8_t command);
static esp_err_t lcd_write_data(const void *data, size_t length);
static esp_err_t lcd_command_data(uint8_t command,
                                  const uint8_t *data,
                                  size_t length);

static esp_err_t lcd_write_command(uint8_t command)
{
    gpio_set_level(CONFIG_C_KEY_TFT_DC_GPIO, 0);
    spi_transaction_t transaction = {
        .length = 8U,
        .tx_buffer = &command,
    };
    return spi_device_polling_transmit(s_lcd, &transaction);
}

static esp_err_t lcd_write_data(const void *data, size_t length)
{
    if (length == 0U) {
        return ESP_OK;
    }
    gpio_set_level(CONFIG_C_KEY_TFT_DC_GPIO, 1);
    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd, &transaction);
}

static esp_err_t lcd_command_data(uint8_t command,
                                  const uint8_t *data,
                                  size_t length)
{
    esp_err_t result = lcd_write_command(command);
    return result == ESP_OK ? lcd_write_data(data, length) : result;
}

static esp_err_t lcd_read_command(uint8_t command,
                                  uint8_t *data,
                                  size_t length)
{
    if (data == NULL || length == 0U ||
        CONFIG_C_KEY_TFT_MISO_GPIO < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = lcd_write_command(command);
    if (result != ESP_OK) {
        return result;
    }

    memset(data, 0, length);
    gpio_set_level(CONFIG_C_KEY_TFT_DC_GPIO, 1);
    spi_transaction_t transaction = {
        .length = length * 8U,
        .rxlength = length * 8U,
        .rx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd, &transaction);
}

static void put_color(uint8_t *buffer,
                      size_t pixel_index,
                      uint16_t color)
{
    buffer[pixel_index * 2U] = (uint8_t)(color >> 8U);
    buffer[pixel_index * 2U + 1U] = (uint8_t)color;
}

static const uint8_t *glyph_for(char value)
{
    static const uint8_t digits[10][TFT_GLYPH_WIDTH] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1E},
    };
    static const uint8_t letters[26][TFT_GLYPH_WIDTH] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E},
        {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22},
        {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41},
        {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A},
        {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41},
        {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F},
        {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E},
        {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E},
        {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F},
        {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x3F, 0x40, 0x38, 0x40, 0x3F},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43},
    };
    static const uint8_t blank[TFT_GLYPH_WIDTH] = {0};
    static const uint8_t colon[TFT_GLYPH_WIDTH] =
        {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t period[TFT_GLYPH_WIDTH] =
        {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t plus[TFT_GLYPH_WIDTH] =
        {0x08, 0x08, 0x3E, 0x08, 0x08};
    static const uint8_t minus[TFT_GLYPH_WIDTH] =
        {0x08, 0x08, 0x08, 0x08, 0x08};

    const unsigned char upper = (unsigned char)toupper((unsigned char)value);
    if (upper >= '0' && upper <= '9') {
        return digits[upper - '0'];
    }
    if (upper >= 'A' && upper <= 'Z') {
        return letters[upper - 'A'];
    }
    switch (upper) {
    case ':':
        return colon;
    case '.':
        return period;
    case '+':
        return plus;
    case '-':
        return minus;
    default:
        return blank;
    }
}

typedef struct {
    uint8_t command;
    uint8_t data[16];
    uint8_t data_length;
    uint16_t delay_ms;
} lcd_init_command_t;

static esp_err_t send_init_sequence(const lcd_init_command_t *sequence,
                                    size_t command_count)
{
    for (size_t i = 0; i < command_count; ++i) {
        esp_err_t result = lcd_command_data(sequence[i].command,
                                            sequence[i].data,
                                            sequence[i].data_length);
        if (result != ESP_OK) {
            return result;
        }
        if (sequence[i].delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(sequence[i].delay_ms));
        }
    }
    return ESP_OK;
}

#ifdef CONFIG_C_KEY_TFT_CONTROLLER_ILI9341
static esp_err_t init_ili9341(void)
{
    static const lcd_init_command_t sequence[] = {
        {LCD_CMD_SWRESET, {0}, 0, 150},
        {0xCF, {0x00, 0xC1, 0x30}, 3, 0},
        {0xED, {0x64, 0x03, 0x12, 0x81}, 4, 0},
        {0xE8, {0x85, 0x00, 0x78}, 3, 0},
        {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
        {0xF7, {0x20}, 1, 0},
        {0xEA, {0x00, 0x00}, 2, 0},
        {0xC0, {0x23}, 1, 0},
        {0xC1, {0x10}, 1, 0},
        {0xC5, {0x3E, 0x28}, 2, 0},
        {0xC7, {0x86}, 1, 0},
        {LCD_CMD_MADCTL, {0x28}, 1, 0},
        {LCD_CMD_COLMOD, {0x55}, 1, 0},
        {0xB1, {0x00, 0x18}, 2, 0},
        {0xB6, {0x08, 0x82, 0x27}, 3, 0},
        {0xF2, {0x00}, 1, 0},
        {0x26, {0x01}, 1, 0},
        {0xE0,
         {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
          0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00},
         15, 0},
        {0xE1,
         {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
          0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F},
         15, 0},
        {LCD_CMD_SLPOUT, {0}, 0, 120},
        {LCD_CMD_DISPON, {0}, 0, 20},
    };
    return send_init_sequence(sequence,
                              sizeof(sequence) / sizeof(sequence[0]));
}
#endif

#ifdef CONFIG_C_KEY_TFT_CONTROLLER_ST7789
static esp_err_t init_st7789(void)
{
    static const lcd_init_command_t sequence[] = {
        {LCD_CMD_SWRESET, {0}, 0, 150},
        {LCD_CMD_SLPOUT, {0}, 0, 120},
        {LCD_CMD_COLMOD, {0x55}, 1, 10},
        {LCD_CMD_MADCTL, {0x28}, 1, 10},
        {0x21, {0}, 0, 10},
        {0x13, {0}, 0, 10},
        {LCD_CMD_DISPON, {0}, 0, 20},
    };
    return send_init_sequence(sequence,
                              sizeof(sequence) / sizeof(sequence[0]));
}
#endif

static void log_controller_id(void)
{
    uint8_t id4[4] = {0};
    uint8_t id[3] = {0};
    const esp_err_t result4 =
        lcd_read_command(LCD_CMD_RDDID4, id4, sizeof(id4));
    const esp_err_t result =
        lcd_read_command(LCD_CMD_RDDID, id, sizeof(id));
    if (result4 == ESP_OK || result == ESP_OK) {
        ESP_LOGI(TAG,
                 "LCD ID: D3=%02X %02X %02X %02X, 04=%02X %02X %02X",
                 id4[0], id4[1], id4[2], id4[3],
                 id[0], id[1], id[2]);
    } else {
        ESP_LOGW(TAG, "LCD ID read unavailable; verify SDO wiring");
    }
}

esp_err_t c_key_tft_init(void)
{
    const gpio_config_t dc_config = {
        .pin_bit_mask = 1ULL << CONFIG_C_KEY_TFT_DC_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&dc_config);
    if (result != ESP_OK) {
        return result;
    }

    if (CONFIG_C_KEY_TFT_BL_GPIO >= 0) {
        const gpio_config_t backlight_config = {
            .pin_bit_mask = 1ULL << CONFIG_C_KEY_TFT_BL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        result = gpio_config(&backlight_config);
        if (result != ESP_OK) {
            return result;
        }
        gpio_set_level(CONFIG_C_KEY_TFT_BL_GPIO,
                       CONFIG_C_KEY_TFT_BL_ACTIVE_HIGH ? 0 : 1);
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = CONFIG_C_KEY_TFT_MOSI_GPIO,
        .miso_io_num = CONFIG_C_KEY_TFT_MISO_GPIO,
        .sclk_io_num = CONFIG_C_KEY_TFT_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_BAND_HEIGHT * 2,
    };
    result = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        return result;
    }

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = CONFIG_C_KEY_TFT_CLOCK_MHZ * 1000 * 1000,
        .mode = 0,
        .spics_io_num = CONFIG_C_KEY_TFT_CS_GPIO,
        .queue_size = 1,
    };
    result = spi_bus_add_device(SPI2_HOST, &device_config, &s_lcd);
    if (result != ESP_OK) {
        return result;
    }

    s_band = heap_caps_malloc(TFT_WIDTH * TFT_BAND_HEIGHT * 2U,
                              MALLOC_CAP_DMA);
    if (s_band == NULL) {
        return ESP_ERR_NO_MEM;
    }

    log_controller_id();
#ifdef CONFIG_C_KEY_TFT_CONTROLLER_ST7789
    result = init_st7789();
    const char *controller = "ST7789";
#else
    result = init_ili9341();
    const char *controller = "ILI9341";
#endif
    if (result != ESP_OK) {
        return result;
    }

    if (CONFIG_C_KEY_TFT_BL_GPIO >= 0) {
        gpio_set_level(CONFIG_C_KEY_TFT_BL_GPIO,
                       CONFIG_C_KEY_TFT_BL_ACTIVE_HIGH ? 1 : 0);
    }
    s_ready = true;
    ESP_LOGI(TAG,
             "%s ready 320x240: MOSI=%d CLK=%d MISO=%d CS=%d DC=%d BL=%d",
             controller,
             CONFIG_C_KEY_TFT_MOSI_GPIO,
             CONFIG_C_KEY_TFT_SCLK_GPIO,
             CONFIG_C_KEY_TFT_MISO_GPIO,
             CONFIG_C_KEY_TFT_CS_GPIO,
             CONFIG_C_KEY_TFT_DC_GPIO,
             CONFIG_C_KEY_TFT_BL_GPIO);
    return ESP_OK;
}

bool c_key_tft_ready(void)
{
    return s_ready;
}

static esp_err_t lcd_set_window(uint16_t x_start,
                                uint16_t y_start,
                                uint16_t x_end,
                                uint16_t y_end)
{
    const uint8_t columns[] = {
        (uint8_t)(x_start >> 8U),
        (uint8_t)x_start,
        (uint8_t)(x_end >> 8U),
        (uint8_t)x_end,
    };
    const uint8_t rows[] = {
        (uint8_t)(y_start >> 8U),
        (uint8_t)y_start,
        (uint8_t)(y_end >> 8U),
        (uint8_t)y_end,
    };
    esp_err_t result =
        lcd_command_data(LCD_CMD_CASET, columns, sizeof(columns));
    if (result == ESP_OK) {
        result = lcd_command_data(LCD_CMD_RASET, rows, sizeof(rows));
    }
    if (result == ESP_OK) {
        result = lcd_write_command(LCD_CMD_RAMWR);
    }
    return result;
}

static void fill_band(uint16_t color)
{
    for (size_t i = 0; i < TFT_WIDTH * TFT_BAND_HEIGHT; ++i) {
        put_color(s_band, i, color);
    }
}

static void draw_global_pixel(int x,
                              int y,
                              int band_y,
                              int band_height,
                              uint16_t color)
{
    if (x < 0 || x >= TFT_WIDTH || y < band_y ||
        y >= band_y + band_height) {
        return;
    }
    put_color(s_band,
              (size_t)(y - band_y) * TFT_WIDTH + (size_t)x,
              color);
}

static void draw_global_rect(int x,
                             int y,
                             int width,
                             int height,
                             int band_y,
                             int band_height,
                             uint16_t color)
{
    const int x_start = x < 0 ? 0 : x;
    const int x_end = x + width > TFT_WIDTH ? TFT_WIDTH : x + width;
    const int y_start = y < band_y ? band_y : y;
    const int y_end = y + height > band_y + band_height
                          ? band_y + band_height
                          : y + height;
    for (int pixel_y = y_start; pixel_y < y_end; ++pixel_y) {
        for (int pixel_x = x_start; pixel_x < x_end; ++pixel_x) {
            draw_global_pixel(pixel_x, pixel_y, band_y, band_height, color);
        }
    }
}

static void draw_ascii_global(char value,
                              int x_origin,
                              int y_origin,
                              int scale,
                              int band_y,
                              int band_height,
                              uint16_t color)
{
    const uint8_t *glyph = glyph_for(value);
    for (int x = 0; x < TFT_GLYPH_WIDTH; ++x) {
        for (int y = 0; y < TFT_GLYPH_HEIGHT; ++y) {
            if ((glyph[x] & (1U << y)) == 0U) {
                continue;
            }
            for (int sx = 0; sx < scale; ++sx) {
                for (int sy = 0; sy < scale; ++sy) {
                    draw_global_pixel(x_origin + x * scale + sx,
                                      y_origin + y * scale + sy,
                                      band_y,
                                      band_height,
                                      color);
                }
            }
        }
    }
}

static void draw_chinese_global(uint32_t codepoint,
                                int x_origin,
                                int y_origin,
                                int band_y,
                                int band_height,
                                uint16_t color)
{
    const uint8_t *glyph = c_key_tft_zh_glyph(codepoint);
    if (glyph == NULL) {
        return;
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const uint8_t bits = glyph[y * 2 + x / 8];
            if ((bits & (1U << (7 - x % 8))) != 0U) {
                draw_global_pixel(x_origin + x,
                                  y_origin + y,
                                  band_y,
                                  band_height,
                                  color);
            }
        }
    }
}

static uint32_t utf8_next(const char **text)
{
    const unsigned char *bytes = (const unsigned char *)*text;
    if (bytes[0] < 0x80U) {
        *text += 1;
        return bytes[0];
    }
    if ((bytes[0] & 0xE0U) == 0xC0U && bytes[1] != 0U) {
        *text += 2;
        return ((uint32_t)(bytes[0] & 0x1FU) << 6U) |
               (uint32_t)(bytes[1] & 0x3FU);
    }
    if ((bytes[0] & 0xF0U) == 0xE0U && bytes[1] != 0U && bytes[2] != 0U) {
        *text += 3;
        return ((uint32_t)(bytes[0] & 0x0FU) << 12U) |
               ((uint32_t)(bytes[1] & 0x3FU) << 6U) |
               (uint32_t)(bytes[2] & 0x3FU);
    }
    *text += 1;
    return '?';
}

static int dashboard_text_width(const char *text, int ascii_scale)
{
    int width = 0;
    while (text != NULL && *text != '\0') {
        const uint32_t codepoint = utf8_next(&text);
        width += codepoint < 0x80U
                     ? (TFT_GLYPH_WIDTH + 1) * ascii_scale
                     : 17;
    }
    return width > 0 ? width - 1 : 0;
}

static void draw_dashboard_text(const char *text,
                                int x,
                                int y,
                                int ascii_scale,
                                int band_y,
                                int band_height,
                                uint16_t color)
{
    while (text != NULL && *text != '\0') {
        const uint32_t codepoint = utf8_next(&text);
        if (codepoint < 0x80U) {
            draw_ascii_global((char)codepoint,
                              x,
                              y + (16 - TFT_GLYPH_HEIGHT * ascii_scale) / 2,
                              ascii_scale,
                              band_y,
                              band_height,
                              color);
            x += (TFT_GLYPH_WIDTH + 1) * ascii_scale;
        } else {
            draw_chinese_global(codepoint,
                                x,
                                y,
                                band_y,
                                band_height,
                                color);
            x += 17;
        }
    }
}

#define COLOR_BACKGROUND 0xFFFFU
#define COLOR_HEADER 0x780FU
#define COLOR_FOOTER 0x2104U
#define COLOR_TEXT 0x18E3U
#define COLOR_MUTED 0x6B4DU
#define COLOR_LINE 0xD69AU
#define COLOR_WHITE 0xFFFFU
#define COLOR_GREEN 0x0640U
#define COLOR_RED 0xB800U
#define COLOR_ORANGE 0xFD20U
#define COLOR_BLUE 0x04DFU

static const char *dashboard_zone_name(c_key_state_t state)
{
    switch (state) {
    case C_KEY_STATE_NO_KEY:
        return "无钥匙";
    case C_KEY_STATE_INVALID_ID:
        return "身份无效";
    case C_KEY_STATE_OUT_OF_ANGLE:
        return "角度超限";
    case C_KEY_STATE_SENSING:
        return "感应区";
    case C_KEY_STATE_WELCOME:
        return "迎宾区";
    case C_KEY_STATE_UNLOCKED:
        return "开锁区";
    case C_KEY_STATE_FAULT:
    default:
        return "系统故障";
    }
}

static uint16_t dashboard_zone_color(c_key_state_t state)
{
    switch (state) {
    case C_KEY_STATE_UNLOCKED:
        return COLOR_GREEN;
    case C_KEY_STATE_WELCOME:
        return COLOR_ORANGE;
    case C_KEY_STATE_SENSING:
        return COLOR_BLUE;
    default:
        return COLOR_RED;
    }
}

static void draw_dashboard_band(const c_key_display_frame_t *frame,
                                int band_y,
                                int band_height)
{
    fill_band(COLOR_BACKGROUND);
    draw_global_rect(0, 0, TFT_WIDTH, 30,
                     band_y, band_height, COLOR_HEADER);
    draw_global_rect(0, 86, TFT_WIDTH, 1,
                     band_y, band_height, COLOR_LINE);
    draw_global_rect(0, 144, TFT_WIDTH, 1,
                     band_y, band_height, COLOR_LINE);
    draw_global_rect(0, 206, TFT_WIDTH, 34,
                     band_y, band_height, COLOR_FOOTER);

    const char *title = "数字钥匙实验系统";
    draw_dashboard_text(title,
                        (TFT_WIDTH - dashboard_text_width(title, 1)) / 2,
                        7,
                        1,
                        band_y,
                        band_height,
                        COLOR_WHITE);

    char value[20];
    draw_dashboard_text("钥匙ID", 10, 38, 1,
                        band_y, band_height, COLOR_TEXT);
    if (frame->key_present) {
        snprintf(value, sizeof(value), "%04u", frame->tag_id);
    } else {
        snprintf(value, sizeof(value), "----");
    }
    draw_dashboard_text(value, 78, 38, 2,
                        band_y, band_height, COLOR_BLUE);

    draw_dashboard_text("门锁ID", 168, 38, 1,
                        band_y, band_height, COLOR_TEXT);
    snprintf(value, sizeof(value), "%04u", frame->accepted_id);
    draw_dashboard_text(value, 238, 38, 2,
                        band_y, band_height, COLOR_BLUE);

    draw_dashboard_text("身份认证", 10, 65, 1,
                        band_y, band_height, COLOR_TEXT);
    const char *authentication = !frame->key_present
                                     ? "无钥匙"
                                     : (frame->authenticated
                                            ? "匹配成功"
                                            : "匹配失败");
    draw_dashboard_text(authentication, 196, 65, 1,
                        band_y, band_height,
                        frame->authenticated ? COLOR_GREEN : COLOR_RED);

    draw_dashboard_text("径向距离", 10, 94, 1,
                        band_y, band_height, COLOR_MUTED);
    draw_dashboard_text("方位角", 174, 94, 1,
                        band_y, band_height, COLOR_MUTED);
    if (frame->pose_valid) {
        snprintf(value, sizeof(value), "%.2f m", frame->distance_m);
    } else {
        snprintf(value, sizeof(value), "--.-- m");
    }
    draw_dashboard_text(value, 10, 118, 2,
                        band_y, band_height, COLOR_TEXT);
    if (frame->pose_valid) {
        snprintf(value, sizeof(value), "%+.1f deg", frame->angle_deg);
    } else {
        snprintf(value, sizeof(value), "--.- deg");
    }
    draw_dashboard_text(value, 174, 118, 2,
                        band_y, band_height, COLOR_TEXT);

    draw_dashboard_text("当前区域", 10, 153, 1,
                        band_y, band_height, COLOR_TEXT);
    draw_dashboard_text(dashboard_zone_name(frame->state), 196, 153, 1,
                        band_y, band_height,
                        dashboard_zone_color(frame->state));

    draw_dashboard_text("门锁状态", 10, 181, 1,
                        band_y, band_height, COLOR_TEXT);
    draw_dashboard_text(frame->unlocked_output ? "开启" : "关闭",
                        230,
                        181,
                        1,
                        band_y,
                        band_height,
                        frame->unlocked_output ? COLOR_GREEN : COLOR_RED);

    draw_dashboard_text("通信", 8, 215, 1,
                        band_y, band_height, COLOR_WHITE);
    draw_dashboard_text(frame->uwb_link_ok ? "正常" : "丢失",
                        47,
                        215,
                        1,
                        band_y,
                        band_height,
                        frame->uwb_link_ok ? COLOR_GREEN : COLOR_RED);
    draw_dashboard_text("迎宾灯", 160, 215, 1,
                        band_y, band_height, COLOR_WHITE);
    draw_dashboard_text(frame->welcome_output ? "开" : "关",
                        218,
                        215,
                        1,
                        band_y,
                        band_height,
                        frame->welcome_output ? COLOR_ORANGE : COLOR_WHITE);
}

esp_err_t c_key_tft_render(const c_key_display_frame_t *frame)
{
    if (!s_ready || frame == NULL || s_band == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int band_y = 0; band_y < TFT_HEIGHT; band_y += TFT_BAND_HEIGHT) {
        const int band_height = band_y + TFT_BAND_HEIGHT <= TFT_HEIGHT
                                    ? TFT_BAND_HEIGHT
                                    : TFT_HEIGHT - band_y;
        draw_dashboard_band(frame, band_y, band_height);
        esp_err_t result = lcd_set_window(
            0,
            (uint16_t)band_y,
            TFT_WIDTH - 1,
            (uint16_t)(band_y + band_height - 1));
        if (result != ESP_OK) {
            return result;
        }
        result = lcd_write_data(s_band,
                                (size_t)TFT_WIDTH * (size_t)band_height * 2U);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

#else

esp_err_t c_key_tft_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool c_key_tft_ready(void)
{
    return false;
}

esp_err_t c_key_tft_render(const c_key_display_frame_t *frame)
{
    (void)frame;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
