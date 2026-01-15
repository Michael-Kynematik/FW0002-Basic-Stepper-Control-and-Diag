#include "neopixel.h"

#include <string.h>
#include <stdio.h>

#include "board.h"
#include "events.h"
#include "neopixel_strip.h"

#include "esp_err.h"

static neopixel_strip_handle_t s_strip = NULL;
static bool s_strip_ready = false;
static neopixel_mode_t s_mode = NEOPIXEL_MODE_OFF;
static uint8_t s_r = 0;
static uint8_t s_g = 0;
static uint8_t s_b = 0;
static uint8_t s_brightness = 32;

static const char *neopixel_mode_to_str(neopixel_mode_t mode)
{
    switch (mode)
    {
    case NEOPIXEL_MODE_OFF:
        return "off";
    case NEOPIXEL_MODE_SOLID:
        return "solid";
    case NEOPIXEL_MODE_BOOTING:
        return "booting";
    case NEOPIXEL_MODE_READY:
        return "ready";
    case NEOPIXEL_MODE_FAULT:
        return "fault";
    default:
        return "unknown";
    }
}

static void neopixel_apply_rgb(void)
{
    if (!s_strip_ready)
    {
        return;
    }
    uint16_t scale = s_brightness;
    uint8_t r = (uint8_t)((s_r * scale) / 255);
    uint8_t g = (uint8_t)((s_g * scale) / 255);
    uint8_t b = (uint8_t)((s_b * scale) / 255);
    neopixel_strip_set_pixel(s_strip, 0, r, g, b);
    neopixel_strip_refresh(s_strip);
}

bool neopixel_init(void)
{
    neopixel_strip_config_t strip_config = {
        .strip_gpio_num = PIN_NEOPIXEL_ONBOARD,
        .max_leds = 1,
        .pixel_format = NEOPIXEL_PIXEL_FORMAT_GRB,
        .model = NEOPIXEL_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };
    neopixel_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };
    esp_err_t err = neopixel_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    s_strip_ready = (err == ESP_OK);
    s_mode = NEOPIXEL_MODE_OFF;
    s_r = 0;
    s_g = 0;
    s_b = 0;
    s_brightness = 32;
    neopixel_apply_rgb();
    return s_strip_ready;
}

bool neopixel_set_mode(neopixel_mode_t mode)
{
    if (mode != s_mode)
    {
        s_mode = mode;
        events_emit("neopixel_mode", "neopixel", 0, neopixel_mode_to_str(mode));
    }
    switch (mode)
    {
    case NEOPIXEL_MODE_OFF:
        s_r = 0;
        s_g = 0;
        s_b = 0;
        break;
    case NEOPIXEL_MODE_BOOTING:
        s_r = 0;
        s_g = 0;
        s_b = 255;
        break;
    case NEOPIXEL_MODE_READY:
        s_r = 0;
        s_g = 255;
        s_b = 0;
        break;
    case NEOPIXEL_MODE_FAULT:
        s_r = 255;
        s_g = 0;
        s_b = 0;
        break;
    case NEOPIXEL_MODE_SOLID:
    default:
        break;
    }
    neopixel_apply_rgb();
    return true;
}

bool neopixel_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    bool mode_changed = (s_mode != NEOPIXEL_MODE_SOLID);
    s_mode = NEOPIXEL_MODE_SOLID;
    s_r = r;
    s_g = g;
    s_b = b;
    if (mode_changed)
    {
        events_emit("neopixel_mode", "neopixel", 0, neopixel_mode_to_str(NEOPIXEL_MODE_SOLID));
    }
    neopixel_apply_rgb();
    return true;
}

bool neopixel_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
    neopixel_apply_rgb();
    return true;
}

uint8_t neopixel_get_brightness(void)
{
    return s_brightness;
}

bool neopixel_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int written = snprintf(buf, len, "{\"mode\":\"%s\",\"rgb\":[%u,%u,%u],\"brightness\":%u}",
                           neopixel_mode_to_str(s_mode),
                           (unsigned)s_r, (unsigned)s_g, (unsigned)s_b,
                           (unsigned)s_brightness);
    return (written >= 0 && (size_t)written < len);
}
