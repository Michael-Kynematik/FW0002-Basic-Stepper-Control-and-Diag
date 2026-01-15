#include "neopixel_strip.h"

#include <stdlib.h>
#include <string.h>

#include "driver/rmt.h"

#define NEOPIXEL_STRIP_RMT_CHANNEL RMT_CHANNEL_0
#define NEOPIXEL_STRIP_CLK_DIV 8
#define NEOPIXEL_STRIP_T0H 4
#define NEOPIXEL_STRIP_T0L 8
#define NEOPIXEL_STRIP_T1H 8
#define NEOPIXEL_STRIP_T1L 4

struct neopixel_strip_t
{
    int gpio;
    uint32_t max_leds;
    uint8_t *pixels;
};

static esp_err_t neopixel_strip_init_rmt(int gpio)
{
    rmt_config_t config = {
        .rmt_mode = RMT_MODE_TX,
        .channel = NEOPIXEL_STRIP_RMT_CHANNEL,
        .gpio_num = gpio,
        .clk_div = NEOPIXEL_STRIP_CLK_DIV,
        .mem_block_num = 1,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_level = RMT_IDLE_LEVEL_LOW,
            .idle_output_en = true,
        },
    };
    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK)
    {
        return err;
    }
    return rmt_driver_install(NEOPIXEL_STRIP_RMT_CHANNEL, 0, 0);
}

esp_err_t neopixel_strip_new_rmt_device(const neopixel_strip_config_t *config,
                                        const neopixel_strip_rmt_config_t *rmt_config,
                                        neopixel_strip_handle_t *out_handle)
{
    (void)rmt_config;
    if (config == NULL || out_handle == NULL || config->max_leds == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = neopixel_strip_init_rmt(config->strip_gpio_num);
    if (err != ESP_OK)
    {
        return err;
    }

    neopixel_strip_handle_t strip = calloc(1, sizeof(*strip));
    if (strip == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    strip->gpio = config->strip_gpio_num;
    strip->max_leds = config->max_leds;
    strip->pixels = calloc(config->max_leds * 3, sizeof(uint8_t));
    if (strip->pixels == NULL)
    {
        free(strip);
        return ESP_ERR_NO_MEM;
    }
    *out_handle = strip;
    return ESP_OK;
}

esp_err_t neopixel_strip_set_pixel(neopixel_strip_handle_t handle, uint32_t index,
                                   uint8_t red, uint8_t green, uint8_t blue)
{
    if (handle == NULL || index >= handle->max_leds)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *p = &handle->pixels[index * 3];
    p[0] = green;
    p[1] = red;
    p[2] = blue;
    return ESP_OK;
}

esp_err_t neopixel_strip_refresh(neopixel_strip_handle_t handle)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t bits_per_led = 24;
    const uint32_t total_bits = handle->max_leds * bits_per_led;
    rmt_item32_t *items = calloc(total_bits, sizeof(rmt_item32_t));
    if (items == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    uint32_t item_index = 0;
    for (uint32_t i = 0; i < handle->max_leds * 3; ++i)
    {
        uint8_t byte = handle->pixels[i];
        for (int bit = 7; bit >= 0; --bit)
        {
            bool one = (byte >> bit) & 0x01;
            rmt_item32_t item = {
                .level0 = 1,
                .duration0 = one ? NEOPIXEL_STRIP_T1H : NEOPIXEL_STRIP_T0H,
                .level1 = 0,
                .duration1 = one ? NEOPIXEL_STRIP_T1L : NEOPIXEL_STRIP_T0L,
            };
            items[item_index++] = item;
        }
    }
    esp_err_t err = rmt_write_items(NEOPIXEL_STRIP_RMT_CHANNEL, items, total_bits, true);
    if (err == ESP_OK)
    {
        err = rmt_wait_tx_done(NEOPIXEL_STRIP_RMT_CHANNEL, pdMS_TO_TICKS(10));
    }
    free(items);
    return err;
}
