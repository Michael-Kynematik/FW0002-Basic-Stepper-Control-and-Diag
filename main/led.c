#include "led.h"

#include <string.h>
#include <stdio.h>

#include "board.h"
#include "events.h"
#include "led_strip.h"

#include "esp_err.h"

static led_strip_handle_t s_strip = NULL;
static bool s_strip_ready = false;
static led_mode_t s_mode = LED_MODE_OFF;
static uint8_t s_r = 0;
static uint8_t s_g = 0;
static uint8_t s_b = 0;

static const char *led_mode_to_str(led_mode_t mode)
{
    switch (mode)
    {
    case LED_MODE_OFF:
        return "off";
    case LED_MODE_SOLID:
        return "solid";
    case LED_MODE_BOOTING:
        return "booting";
    case LED_MODE_READY:
        return "ready";
    case LED_MODE_FAULT:
        return "fault";
    default:
        return "unknown";
    }
}

static void led_apply_rgb(void)
{
    if (!s_strip_ready)
    {
        return;
    }
    led_strip_set_pixel(s_strip, 0, s_r, s_g, s_b);
    led_strip_refresh(s_strip);
}

bool led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_NEOPIXEL_ONBOARD,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    s_strip_ready = (err == ESP_OK);
    s_mode = LED_MODE_OFF;
    s_r = 0;
    s_g = 0;
    s_b = 0;
    led_apply_rgb();
    return s_strip_ready;
}

bool led_set_mode(led_mode_t mode)
{
    if (mode != s_mode)
    {
        s_mode = mode;
        events_emit("led_mode", "led", 0, led_mode_to_str(mode));
    }
    switch (mode)
    {
    case LED_MODE_OFF:
        s_r = 0;
        s_g = 0;
        s_b = 0;
        break;
    case LED_MODE_BOOTING:
        s_r = 0;
        s_g = 0;
        s_b = 255;
        break;
    case LED_MODE_READY:
        s_r = 0;
        s_g = 255;
        s_b = 0;
        break;
    case LED_MODE_FAULT:
        s_r = 255;
        s_g = 0;
        s_b = 0;
        break;
    case LED_MODE_SOLID:
    default:
        break;
    }
    led_apply_rgb();
    return true;
}

bool led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    bool mode_changed = (s_mode != LED_MODE_SOLID);
    s_mode = LED_MODE_SOLID;
    s_r = r;
    s_g = g;
    s_b = b;
    if (mode_changed)
    {
        events_emit("led_mode", "led", 0, led_mode_to_str(LED_MODE_SOLID));
    }
    led_apply_rgb();
    return true;
}

bool led_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int written = snprintf(buf, len, "{\"mode\":\"%s\",\"rgb\":[%u,%u,%u]}",
                           led_mode_to_str(s_mode), (unsigned)s_r, (unsigned)s_g, (unsigned)s_b);
    return (written >= 0 && (size_t)written < len);
}
