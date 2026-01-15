#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t loadcell_scale_init(void);
esp_err_t loadcell_scale_read_raw(int samples, int32_t *raw);
esp_err_t loadcell_scale_read_grams(int samples, float *grams);
esp_err_t loadcell_scale_raw_to_grams(int32_t raw, float *grams);
esp_err_t loadcell_scale_tare(int samples);
esp_err_t loadcell_scale_calibrate(int samples, float known_grams);
bool loadcell_scale_get_status_json(char *buf, size_t len);
bool loadcell_scale_is_calibrated(void);
