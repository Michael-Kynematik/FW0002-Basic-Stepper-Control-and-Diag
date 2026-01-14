#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct led_strip_t *led_strip_handle_t;

typedef enum
{
    LED_PIXEL_FORMAT_GRB = 0,
} led_pixel_format_t;

typedef enum
{
    LED_MODEL_WS2812 = 0,
} led_model_t;

typedef struct
{
    int strip_gpio_num;
    uint32_t max_leds;
    led_pixel_format_t led_pixel_format;
    led_model_t led_model;
    struct
    {
        unsigned int invert_out : 1;
    } flags;
} led_strip_config_t;

typedef struct
{
    int clk_src;
    uint32_t resolution_hz;
    uint32_t mem_block_symbols;
    struct
    {
        unsigned int with_dma : 1;
    } flags;
} led_strip_rmt_config_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *out_handle);
esp_err_t led_strip_set_pixel(led_strip_handle_t handle, uint32_t index,
                              uint8_t red, uint8_t green, uint8_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t handle);
