#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t loadcell_adc_init(void);
bool loadcell_adc_is_ready(void);
esp_err_t loadcell_adc_read_raw(int32_t *out);
esp_err_t loadcell_adc_read_average(int samples, int32_t *avg_out);
void loadcell_adc_power_down(void);
void loadcell_adc_power_up(void);
