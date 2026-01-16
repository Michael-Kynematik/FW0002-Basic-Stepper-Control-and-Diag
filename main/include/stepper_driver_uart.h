#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t stepper_driver_uart_init(void);
esp_err_t stepper_driver_ping(void);
esp_err_t stepper_driver_read_ifcnt(uint8_t *out);
esp_err_t stepper_driver_set_stealthchop(bool enable);
esp_err_t stepper_driver_set_microsteps(uint16_t microsteps);
esp_err_t stepper_driver_set_current(uint8_t run, uint8_t hold, uint8_t hold_delay);
esp_err_t stepper_driver_clear_faults(void);
bool stepper_driver_get_status_json(char *buf, size_t len);
