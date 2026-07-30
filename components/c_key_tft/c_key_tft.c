#include "c_key_tft.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
#define TFT_SCALE 2
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

static void draw_character(char value,
                           int x_origin,
                           int y_origin,
                           uint16_t color)
{
    const uint8_t *glyph = glyph_for(value);
    for (int x = 0; x < TFT_GLYPH_WIDTH; ++x) {
        for (int y = 0; y < TFT_GLYPH_HEIGHT; ++y) {
            if ((glyph[x] & (1U << y)) == 0U) {
                continue;
            }
            for (int sx = 0; sx < TFT_SCALE; ++sx) {
                for (int sy = 0; sy < TFT_SCALE; ++sy) {
                    const int pixel_x = x_origin + x * TFT_SCALE + sx;
                    const int pixel_y = y_origin + y * TFT_SCALE + sy;
                    if (pixel_x >= 0 && pixel_x < TFT_WIDTH &&
                        pixel_y >= 0 && pixel_y < TFT_BAND_HEIGHT) {
                        put_color(s_band,
                                  (size_t)pixel_y * TFT_WIDTH +
                                      (size_t)pixel_x,
                                  color);
                    }
                }
            }
        }
    }
}

static esp_err_t clear_screen(void)
{
    fill_band(0x0000U);
    for (int y = 0; y < TFT_HEIGHT; y += TFT_BAND_HEIGHT) {
        const int height =
            y + TFT_BAND_HEIGHT <= TFT_HEIGHT
                ? TFT_BAND_HEIGHT
                : TFT_HEIGHT - y;
        esp_err_t result = lcd_set_window(
            0, (uint16_t)y, TFT_WIDTH - 1, (uint16_t)(y + height - 1));
        if (result != ESP_OK) {
            return result;
        }
        result = lcd_write_data(s_band,
                                (size_t)TFT_WIDTH * (size_t)height * 2U);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t c_key_tft_render(const c_key_display_frame_t *frame)
{
    if (!s_ready || frame == NULL || s_band == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    static bool first_render = true;
    if (first_render) {
        esp_err_t result = clear_screen();
        if (result != ESP_OK) {
            return result;
        }
        first_render = false;
    }

    static const uint16_t line_colors[C_KEY_DISPLAY_LINE_COUNT] = {
        0x07FFU,
        0xFFFFU,
        0xBDF7U,
        0xFFE0U,
        0x07E0U,
        0xF81FU,
    };
    const size_t maximum_characters =
        (TFT_WIDTH - 8U) / ((TFT_GLYPH_WIDTH + 1U) * TFT_SCALE);

    for (size_t line_index = 0;
         line_index < C_KEY_DISPLAY_LINE_COUNT;
         ++line_index) {
        fill_band(0x0000U);
        const char *text = frame->lines[line_index];
        const size_t length = strnlen(text, C_KEY_DISPLAY_LINE_LENGTH);
        const size_t visible =
            length < maximum_characters ? length : maximum_characters;
        for (size_t character = 0; character < visible; ++character) {
            draw_character(text[character],
                           4 + (int)character *
                                   (TFT_GLYPH_WIDTH + 1) * TFT_SCALE,
                           7,
                           line_colors[line_index]);
        }

        const uint16_t y_start = (uint16_t)(4U + line_index * 38U);
        esp_err_t result = lcd_set_window(
            0, y_start, TFT_WIDTH - 1, y_start + TFT_BAND_HEIGHT - 1);
        if (result != ESP_OK) {
            return result;
        }
        result = lcd_write_data(s_band,
                                TFT_WIDTH * TFT_BAND_HEIGHT * 2U);
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
