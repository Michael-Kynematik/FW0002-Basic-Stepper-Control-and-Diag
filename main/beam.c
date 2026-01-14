#include "beam.h"

#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"

bool beam_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BEAM_INPUT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // default pull-up for break-beam
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&cfg) == ESP_OK);
}

int beam_read(void)
{
    return gpio_get_level(PIN_BEAM_INPUT);
}

bool beam_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int value = beam_read();
    int written = snprintf(buf, len, "{\"beam_state\":%d}", value);
    return (written >= 0 && (size_t)written < len);
}
