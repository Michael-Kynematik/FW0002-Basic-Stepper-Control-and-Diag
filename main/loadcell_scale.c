#include "loadcell_scale.h"

#include <stdio.h>

#include "events.h"
#include "loadcell_adc.h"

#define SCALE_DEFAULT_SAMPLES 5

static int32_t s_tare_offset_raw = 0;
static float s_scale_factor_raw_per_gram = 0.0f;
static bool s_calibrated = false;

esp_err_t loadcell_scale_init(void)
{
    s_tare_offset_raw = 0;
    s_scale_factor_raw_per_gram = 0.0f;
    s_calibrated = false;
    return loadcell_adc_init();
}

bool loadcell_scale_is_calibrated(void)
{
    return s_calibrated;
}

esp_err_t loadcell_scale_read_raw(int samples, int32_t *raw)
{
    if (samples <= 0 || raw == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return loadcell_adc_read_average(samples, raw);
}

esp_err_t loadcell_scale_raw_to_grams(int32_t raw, float *grams)
{
    if (grams == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_calibrated || s_scale_factor_raw_per_gram == 0.0f)
    {
        return ESP_ERR_INVALID_STATE;
    }
    *grams = (raw - s_tare_offset_raw) / s_scale_factor_raw_per_gram;
    return ESP_OK;
}

esp_err_t loadcell_scale_read_grams(int samples, float *grams)
{
    if (grams == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t raw = 0;
    esp_err_t err = loadcell_scale_read_raw(samples, &raw);
    if (err != ESP_OK)
    {
        return err;
    }
    return loadcell_scale_raw_to_grams(raw, grams);
}

esp_err_t loadcell_scale_tare(int samples)
{
    int32_t raw = 0;
    esp_err_t err = loadcell_scale_read_raw(samples, &raw);
    if (err != ESP_OK)
    {
        return err;
    }
    s_tare_offset_raw = raw;
    events_emit("scale_tare", "scale", 0, "set");
    return ESP_OK;
}

esp_err_t loadcell_scale_calibrate(int samples, float known_grams)
{
    if (!(known_grams > 0.0f))
    {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t raw = 0;
    esp_err_t err = loadcell_scale_read_raw(samples, &raw);
    if (err != ESP_OK)
    {
        return err;
    }
    int32_t delta = raw - s_tare_offset_raw;
    if (delta == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_scale_factor_raw_per_gram = (float)delta / known_grams;
    s_calibrated = true;
    char reason[EVENTS_REASON_MAX];
    int written = snprintf(reason, sizeof(reason), "set %.3fg", (double)known_grams);
    if (written < 0)
    {
        reason[0] = '\0';
    }
    events_emit("scale_cal", "scale", 0, reason);
    return ESP_OK;
}

bool loadcell_scale_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    int32_t raw = 0;
    bool has_raw = (loadcell_scale_read_raw(SCALE_DEFAULT_SAMPLES, &raw) == ESP_OK);
    float grams = 0.0f;
    bool has_grams = false;
    if (has_raw && s_calibrated)
    {
        has_grams = (loadcell_scale_raw_to_grams(raw, &grams) == ESP_OK);
    }

    char raw_buf[24];
    char grams_buf[32];
    const char *raw_str = "null";
    const char *grams_str = "null";
    if (has_raw)
    {
        snprintf(raw_buf, sizeof(raw_buf), "%ld", (long)raw);
        raw_str = raw_buf;
    }
    if (has_grams)
    {
        snprintf(grams_buf, sizeof(grams_buf), "%.3f", (double)grams);
        grams_str = grams_buf;
    }

    int written = snprintf(buf, len,
                           "{\"raw\":%s,\"grams\":%s,"
                           "\"tare_offset_raw\":%ld,"
                           "\"scale_factor\":%.6f,"
                           "\"calibrated\":%s}",
                           raw_str,
                           grams_str,
                           (long)s_tare_offset_raw,
                           (double)s_scale_factor_raw_per_gram,
                           s_calibrated ? "true" : "false");
    return (written >= 0 && (size_t)written < len);
}
