#pragma once

#include <stdbool.h>
#include <stddef.h>

bool beam_init(void);
int beam_read(void);
bool beam_get_status_json(char *buf, size_t len);
