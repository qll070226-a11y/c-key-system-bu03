#include "c_key_io.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "sdkconfig.h"

static const int s_dip_pins[4] = {
    CONFIG_C_KEY_DIP_BIT0_GPIO,
    CONFIG_C_KEY_DIP_BIT1_GPIO,
    CONFIG_C_KEY_DIP_BIT2_GPIO,
    CONFIG_C_KEY_DIP_BIT3_GPIO,
};

static bool pin_enabled(int pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX;
}

static esp_err_t configure_output(int pin)
{
    if (!pin_enabled(pin)) {
        return ESP_OK;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t configure_input(int pin)
{
    if (!pin_enabled(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static void set_output(int pin, bool on)
{
    if (!pin_enabled(pin)) {
        return;
    }

    const int active_level = CONFIG_C_KEY_OUTPUT_ACTIVE_HIGH ? 1 : 0;
    gpio_set_level((gpio_num_t)pin, on ? active_level : !active_level);
}

bool c_key_io_dip_available(void)
{
    for (size_t i = 0; i < 4U; ++i) {
        if (!pin_enabled(s_dip_pins[i])) {
            return false;
        }
    }
    return true;
}

static bool read_dip(uint8_t *value)
{
    if (value == NULL || !c_key_io_dip_available()) {
        return false;
    }

    uint8_t result = 0;
    for (size_t i = 0; i < 4U; ++i) {
        if (gpio_get_level((gpio_num_t)s_dip_pins[i]) == 0) {
            result |= (uint8_t)(1U << i);
        }
    }
    *value = result;
    return true;
}

esp_err_t c_key_io_init(c_key_io_t *io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(io, 0, sizeof(*io));

    const int outputs[] = {
        CONFIG_C_KEY_RED_LED_GPIO,
        CONFIG_C_KEY_GREEN_LED_GPIO,
        CONFIG_C_KEY_WELCOME_LED_GPIO,
        CONFIG_C_KEY_BUZZER_GPIO,
    };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        const esp_err_t result = configure_output(outputs[i]);
        if (result != ESP_OK) {
            return result;
        }
    }

    if (c_key_io_dip_available()) {
        for (size_t i = 0; i < 4U; ++i) {
            const esp_err_t result = configure_input(s_dip_pins[i]);
            if (result != ESP_OK) {
                return result;
            }
        }
    }

    io->initialized = true;
    c_key_io_force_safe(io);
    return ESP_OK;
}

bool c_key_io_poll_accepted_id(c_key_io_t *io,
                               uint32_t now_ms,
                               uint8_t *accepted_id,
                               bool *changed)
{
    if (io == NULL || accepted_id == NULL || changed == NULL || !io->initialized) {
        return false;
    }

    uint8_t raw_id;
    if (!read_dip(&raw_id)) {
        return false;
    }

    *changed = false;
    if (!io->dip_initialized) {
        io->candidate_id = raw_id;
        io->stable_id = raw_id;
        io->candidate_since_ms = now_ms;
        io->dip_initialized = true;
    } else if (raw_id != io->candidate_id) {
        io->candidate_id = raw_id;
        io->candidate_since_ms = now_ms;
    } else if (raw_id != io->stable_id &&
               now_ms - io->candidate_since_ms >= CONFIG_C_KEY_DIP_DEBOUNCE_MS) {
        io->stable_id = raw_id;
        *changed = true;
    }

    *accepted_id = io->stable_id;
    return true;
}

void c_key_io_apply_state(c_key_io_t *io,
                          c_key_state_t state,
                          uint32_t events,
                          uint32_t now_ms)
{
    if (io == NULL || !io->initialized) {
        return;
    }

    if ((events & C_KEY_EVENT_WELCOME_ON) != 0U) {
        io->buzzer_active = true;
        io->buzzer_until_ms = now_ms + CONFIG_C_KEY_BUZZER_PULSE_MS;
    }
    if (io->buzzer_active && (int32_t)(now_ms - io->buzzer_until_ms) >= 0) {
        io->buzzer_active = false;
    }

    const bool unlocked = state == C_KEY_STATE_UNLOCKED;
    const bool welcome = state == C_KEY_STATE_WELCOME || unlocked;
    set_output(CONFIG_C_KEY_RED_LED_GPIO, !unlocked);
    set_output(CONFIG_C_KEY_GREEN_LED_GPIO, unlocked);
    set_output(CONFIG_C_KEY_WELCOME_LED_GPIO, welcome);
    set_output(CONFIG_C_KEY_BUZZER_GPIO, io->buzzer_active);
}

void c_key_io_force_safe(c_key_io_t *io)
{
    if (io == NULL) {
        return;
    }

    io->buzzer_active = false;
    set_output(CONFIG_C_KEY_RED_LED_GPIO, true);
    set_output(CONFIG_C_KEY_GREEN_LED_GPIO, false);
    set_output(CONFIG_C_KEY_WELCOME_LED_GPIO, false);
    set_output(CONFIG_C_KEY_BUZZER_GPIO, false);
}

