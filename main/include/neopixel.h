#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    NEOPIXEL_MODE_OFF = 0,
    NEOPIXEL_MODE_SOLID,
    NEOPIXEL_MODE_BOOTING,
    NEOPIXEL_MODE_READY,
    NEOPIXEL_MODE_FAULT,
} neopixel_mode_t;

bool neopixel_init(void);
bool neopixel_set_mode(neopixel_mode_t mode);
bool neopixel_set_rgb(uint8_t r, uint8_t g, uint8_t b);
bool neopixel_get_status_json(char *buf, size_t len);
