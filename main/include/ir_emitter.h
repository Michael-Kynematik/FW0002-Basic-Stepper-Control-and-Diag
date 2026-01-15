#pragma once

#include <stdbool.h>
#include <stddef.h>

bool ir_emitter_init(void);
bool ir_emitter_set(bool on);
bool ir_emitter_get_status_json(char *buf, size_t len);
