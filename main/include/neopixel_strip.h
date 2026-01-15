#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct neopixel_strip_t *neopixel_strip_handle_t;

typedef enum
{
    NEOPIXEL_PIXEL_FORMAT_GRB = 0,
} neopixel_pixel_format_t;

typedef enum
{
    NEOPIXEL_MODEL_WS2812 = 0,
} neopixel_model_t;

typedef struct
{
    int strip_gpio_num;
    uint32_t max_leds;
    neopixel_pixel_format_t pixel_format;
    neopixel_model_t model;
    struct
    {
        unsigned int invert_out : 1;
    } flags;
} neopixel_strip_config_t;

typedef struct
{
    int clk_src;
    uint32_t resolution_hz;
    uint32_t mem_block_symbols;
    struct
    {
        unsigned int with_dma : 1;
    } flags;
} neopixel_strip_rmt_config_t;

esp_err_t neopixel_strip_new_rmt_device(const neopixel_strip_config_t *config,
                                        const neopixel_strip_rmt_config_t *rmt_config,
                                        neopixel_strip_handle_t *out_handle);
esp_err_t neopixel_strip_set_pixel(neopixel_strip_handle_t handle, uint32_t index,
                                   uint8_t red, uint8_t green, uint8_t blue);
esp_err_t neopixel_strip_refresh(neopixel_strip_handle_t handle);
