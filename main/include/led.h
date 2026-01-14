#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    LED_MODE_OFF = 0,
    LED_MODE_SOLID,
    LED_MODE_BOOTING,
    LED_MODE_READY,
    LED_MODE_FAULT,
} led_mode_t;

bool led_init(void);
bool led_set_mode(led_mode_t mode);
bool led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
bool led_get_status_json(char *buf, size_t len);
