#pragma once

#include <stdbool.h>
#include <stddef.h>

bool ir_sensor_init(void);
int ir_sensor_read(void);
bool ir_sensor_get_status_json(char *buf, size_t len);
