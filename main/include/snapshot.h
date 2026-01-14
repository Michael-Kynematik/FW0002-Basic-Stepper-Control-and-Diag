#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool (*snapshot_value_fn)(char *buf, size_t len, size_t *used);

bool snapshot_register_field(const char *key, snapshot_value_fn value_fn);
bool snapshot_build(char *buf, size_t len);

bool snapshot_append_u32(char *buf, size_t len, size_t *used, uint32_t value);
bool snapshot_append_i64(char *buf, size_t len, size_t *used, int64_t value);
bool snapshot_append_bool(char *buf, size_t len, size_t *used, bool value);
bool snapshot_append_string(char *buf, size_t len, size_t *used, const char *value);
bool snapshot_append_raw(char *buf, size_t len, size_t *used, const char *value);
