#pragma once

#include <stdbool.h>
#include <stddef.h>

bool ir_init(void);
bool ir_set(bool on);
bool ir_get_status_json(char *buf, size_t len);
