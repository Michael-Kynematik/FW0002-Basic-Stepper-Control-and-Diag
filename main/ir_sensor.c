#include "ir_sensor.h"

#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"

bool ir_sensor_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_IR_SENSOR_INPUT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // default pull-up for break-beam
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&cfg) == ESP_OK);
}

int ir_sensor_read(void)
{
    return gpio_get_level(PIN_IR_SENSOR_INPUT);
}

bool ir_sensor_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int value = ir_sensor_read();
    int written = snprintf(buf, len, "{\"ir_sensor_state\":%d}", value);
    return (written >= 0 && (size_t)written < len);
}
