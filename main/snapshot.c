#include "snapshot.h"

#include <string.h>
#include <stdio.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "fw_version.h"
#include "board.h"
#include "loadcell_scale.h"
#include "motor.h"
#include "reset_reason.h"

#define SNAPSHOT_MAX_FIELDS 16

typedef struct
{
    const char *key;
    snapshot_value_fn value_fn;
} snapshot_field_t;

static snapshot_field_t s_fields[SNAPSHOT_MAX_FIELDS];
static size_t s_field_count = 0;
static bool s_defaults_registered = false;

static bool snapshot_append_char(char *buf, size_t len, size_t *used, char c)
{
    if (*used + 1 >= len)
    {
        return false;
    }
    buf[*used] = c;
    *used += 1;
    buf[*used] = '\0';
    return true;
}

static bool snapshot_append_bytes(char *buf, size_t len, size_t *used, const char *src, size_t src_len)
{
    if (*used + src_len >= len)
    {
        return false;
    }
    memcpy(&buf[*used], src, src_len);
    *used += src_len;
    buf[*used] = '\0';
    return true;
}

bool snapshot_append_raw(char *buf, size_t len, size_t *used, const char *value)
{
    if (value == NULL)
    {
        return false;
    }
    return snapshot_append_bytes(buf, len, used, value, strlen(value));
}

bool snapshot_append_u32(char *buf, size_t len, size_t *used, uint32_t value)
{
    char tmp[16];
    int written = snprintf(tmp, sizeof(tmp), "%u", (unsigned)value);
    if (written < 0 || (size_t)written >= sizeof(tmp))
    {
        return false;
    }
    return snapshot_append_bytes(buf, len, used, tmp, (size_t)written);
}

bool snapshot_append_i64(char *buf, size_t len, size_t *used, int64_t value)
{
    char tmp[24];
    int written = snprintf(tmp, sizeof(tmp), "%lld", (long long)value);
    if (written < 0 || (size_t)written >= sizeof(tmp))
    {
        return false;
    }
    return snapshot_append_bytes(buf, len, used, tmp, (size_t)written);
}

bool snapshot_append_bool(char *buf, size_t len, size_t *used, bool value)
{
    return snapshot_append_raw(buf, len, used, value ? "true" : "false");
}

bool snapshot_append_string(char *buf, size_t len, size_t *used, const char *value)
{
    if (value == NULL)
    {
        return false;
    }
    if (!snapshot_append_char(buf, len, used, '"'))
    {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p)
    {
        unsigned char c = *p;
        if (c == '"' || c == '\\')
        {
            if (!snapshot_append_char(buf, len, used, '\\') ||
                !snapshot_append_char(buf, len, used, (char)c))
            {
                return false;
            }
        }
        else if (c < 0x20)
        {
            char esc[7];
            int written = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)c);
            if (written != 6 || !snapshot_append_bytes(buf, len, used, esc, 6))
            {
                return false;
            }
        }
        else
        {
            if (!snapshot_append_char(buf, len, used, (char)c))
            {
                return false;
            }
        }
    }
    return snapshot_append_char(buf, len, used, '"');
}

static bool snapshot_field_uptime(char *buf, size_t len, size_t *used)
{
    return snapshot_append_i64(buf, len, used, esp_timer_get_time() / 1000);
}

static bool snapshot_field_heap_free(char *buf, size_t len, size_t *used)
{
    return snapshot_append_u32(buf, len, used, esp_get_free_heap_size());
}

static bool snapshot_field_heap_min_free(char *buf, size_t len, size_t *used)
{
    return snapshot_append_u32(buf, len, used, esp_get_minimum_free_heap_size());
}

static bool snapshot_field_reset_reason(char *buf, size_t len, size_t *used)
{
    return snapshot_append_string(buf, len, used, reset_reason_to_str(esp_reset_reason()));
}

static bool snapshot_field_fw_version(char *buf, size_t len, size_t *used)
{
    return snapshot_append_string(buf, len, used, FW_VERSION);
}

static bool snapshot_field_fw_build(char *buf, size_t len, size_t *used)
{
    return snapshot_append_string(buf, len, used, FW_BUILD);
}

static bool snapshot_field_schema_version(char *buf, size_t len, size_t *used)
{
    return snapshot_append_u32(buf, len, used, (uint32_t)SNAPSHOT_SCHEMA_VERSION);
}

static bool snapshot_field_device_id(char *buf, size_t len, size_t *used)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    {
        return false;
    }
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return snapshot_append_string(buf, len, used, id);
}

static bool snapshot_field_hw_rev(char *buf, size_t len, size_t *used)
{
    return snapshot_append_u32(buf, len, used, (uint32_t)HW_REV);
}

static bool snapshot_field_board_safe(char *buf, size_t len, size_t *used)
{
    return snapshot_append_bool(buf, len, used, board_is_safe());
}

static bool snapshot_field_scale(char *buf, size_t len, size_t *used)
{
    char scale_json[192];
    if (!loadcell_scale_get_status_json(scale_json, sizeof(scale_json)))
    {
        return snapshot_append_raw(buf, len, used,
                                   "{\"raw\":null,\"grams\":null,"
                                   "\"tare_offset_raw\":0,\"scale_factor\":0.0,"
                                   "\"calibrated\":false}");
    }
    return snapshot_append_raw(buf, len, used, scale_json);
}

static bool snapshot_field_motor(char *buf, size_t len, size_t *used)
{
    char motor_json[192];
    if (!motor_get_status_json(motor_json, sizeof(motor_json)))
    {
        return snapshot_append_raw(buf, len, used,
                                   "{\"state\":\"disabled\",\"enabled\":false,"
                                   "\"step_hz\":0,\"dir\":\"CW\","
                                   "\"fault_code\":0,\"fault_reason\":\"none\"}");
    }
    return snapshot_append_raw(buf, len, used, motor_json);
}

static bool snapshot_register_defaults(void)
{
    if (s_defaults_registered)
    {
        return true;
    }
    if (!snapshot_register_field("uptime_ms", snapshot_field_uptime) ||
        !snapshot_register_field("heap_free_bytes", snapshot_field_heap_free) ||
        !snapshot_register_field("heap_min_free_bytes", snapshot_field_heap_min_free) ||
        !snapshot_register_field("reset_reason", snapshot_field_reset_reason) ||
        !snapshot_register_field("fw_version", snapshot_field_fw_version) ||
        !snapshot_register_field("fw_build", snapshot_field_fw_build) ||
        !snapshot_register_field("schema_version", snapshot_field_schema_version) ||
        !snapshot_register_field("device_id", snapshot_field_device_id) ||
        !snapshot_register_field("hw_rev", snapshot_field_hw_rev) ||
        !snapshot_register_field("board_safe", snapshot_field_board_safe) ||
        !snapshot_register_field("scale", snapshot_field_scale) ||
        !snapshot_register_field("motor", snapshot_field_motor))
    {
        return false;
    }
    s_defaults_registered = true;
    return true;
}

bool snapshot_register_field(const char *key, snapshot_value_fn value_fn)
{
    if (key == NULL || value_fn == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < s_field_count; ++i)
    {
        if (strcmp(s_fields[i].key, key) == 0)
        {
            return false;
        }
    }
    if (s_field_count >= SNAPSHOT_MAX_FIELDS)
    {
        return false;
    }
    s_fields[s_field_count].key = key;
    s_fields[s_field_count].value_fn = value_fn;
    s_field_count++;
    return true;
}

bool snapshot_build(char *buf, size_t len)
{
    if (!snapshot_register_defaults())
    {
        return false;
    }
    if (buf == NULL || len < 3)
    {
        return false;
    }
    size_t used = 0;
    buf[0] = '\0';
    if (!snapshot_append_char(buf, len, &used, '{'))
    {
        return false;
    }
    for (size_t i = 0; i < s_field_count; ++i)
    {
        if (i > 0 && !snapshot_append_char(buf, len, &used, ','))
        {
            return false;
        }
        if (!snapshot_append_string(buf, len, &used, s_fields[i].key))
        {
            return false;
        }
        if (!snapshot_append_char(buf, len, &used, ':'))
        {
            return false;
        }
        if (!s_fields[i].value_fn(buf, len, &used))
        {
            return false;
        }
    }
    return snapshot_append_char(buf, len, &used, '}');
}
