#include "loadcell_adc.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define LOADCELL_ADC_GAIN_PULSES 1
#define LOADCELL_ADC_READY_TIMEOUT_US 100000
#define LOADCELL_ADC_SCK_HIGH_US 1
#define LOADCELL_ADC_SCK_LOW_US 1

static inline void loadcell_adc_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

esp_err_t loadcell_adc_init(void)
{
    gpio_config_t sck_cfg = {
        .pin_bit_mask = (1ULL << PIN_LOADCELL_ADC_SCK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&sck_cfg);
    if (err != ESP_OK)
    {
        return err;
    }
    gpio_config_t dout_cfg = {
        .pin_bit_mask = (1ULL << PIN_LOADCELL_ADC_DOUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&dout_cfg);
    if (err != ESP_OK)
    {
        return err;
    }
    gpio_set_level(PIN_LOADCELL_ADC_SCK, 0);
    return ESP_OK;
}

bool loadcell_adc_is_ready(void)
{
    return (gpio_get_level(PIN_LOADCELL_ADC_DOUT) == 0);
}

static esp_err_t loadcell_adc_wait_ready(void)
{
    int64_t start_us = esp_timer_get_time();
    while (!loadcell_adc_is_ready())
    {
        if ((esp_timer_get_time() - start_us) > LOADCELL_ADC_READY_TIMEOUT_US)
        {
            return ESP_ERR_TIMEOUT;
        }
        loadcell_adc_delay_us(10);
    }
    return ESP_OK;
}

static uint32_t loadcell_adc_shift_read(void)
{
    uint32_t value = 0;
    for (int i = 0; i < 24; ++i)
    {
        gpio_set_level(PIN_LOADCELL_ADC_SCK, 1);
        loadcell_adc_delay_us(LOADCELL_ADC_SCK_HIGH_US);
        value = (value << 1) | (uint32_t)gpio_get_level(PIN_LOADCELL_ADC_DOUT);
        gpio_set_level(PIN_LOADCELL_ADC_SCK, 0);
        loadcell_adc_delay_us(LOADCELL_ADC_SCK_LOW_US);
    }
    for (int i = 0; i < LOADCELL_ADC_GAIN_PULSES; ++i)
    {
        gpio_set_level(PIN_LOADCELL_ADC_SCK, 1);
        loadcell_adc_delay_us(LOADCELL_ADC_SCK_HIGH_US);
        gpio_set_level(PIN_LOADCELL_ADC_SCK, 0);
        loadcell_adc_delay_us(LOADCELL_ADC_SCK_LOW_US);
    }
    return value;
}

esp_err_t loadcell_adc_read_raw(int32_t *out)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = loadcell_adc_wait_ready();
    if (err != ESP_OK)
    {
        return err;
    }
    uint32_t raw = loadcell_adc_shift_read();
    if (raw & 0x800000)
    {
        raw |= 0xFF000000;
    }
    *out = (int32_t)raw;
    return ESP_OK;
}

esp_err_t loadcell_adc_read_average(int samples, int32_t *avg_out)
{
    if (samples <= 0 || avg_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t sum = 0;
    for (int i = 0; i < samples; ++i)
    {
        int32_t value = 0;
        esp_err_t err = loadcell_adc_read_raw(&value);
        if (err != ESP_OK)
        {
            return err;
        }
        sum += value;
    }
    *avg_out = (int32_t)(sum / samples);
    return ESP_OK;
}

