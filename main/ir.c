#include "ir.h"

#include <stdio.h>

#include "board.h"
#include "events.h"

#include "driver/gpio.h"

static bool s_ir_on = false;

bool ir_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_IR_EMITTER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK)
    {
        return false;
    }
    s_ir_on = false;
    gpio_set_level(PIN_IR_EMITTER, 0);
    return true;
}

bool ir_set(bool on)
{
    if (gpio_set_level(PIN_IR_EMITTER, on ? 1 : 0) != ESP_OK)
    {
        return false;
    }
    if (s_ir_on != on)
    {
        s_ir_on = on;
        events_emit("ir", "ir", 0, on ? "on" : "off");
    }
    return true;
}

bool ir_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int written = snprintf(buf, len, "{\"ir_on\":%s}", s_ir_on ? "true" : "false");
    return (written >= 0 && (size_t)written < len);
}
