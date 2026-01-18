#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STEPPER_TMC_REG_GCONF    0x00
#define STEPPER_TMC_REG_IFCNT    0x02
#define STEPPER_TMC_REG_CHOPCONF 0x6C

#define STEPPER_TMC_GCONF_PDN_DISABLE       (1u << 6)
#define STEPPER_TMC_GCONF_MSTEP_REG_SELECT  (1u << 7)
#define STEPPER_TMC_GCONF_I_SCALE_ANALOG    (1u << 0)

esp_err_t stepper_uart_read_reg(uint8_t slave, uint8_t reg, uint32_t *out);
esp_err_t stepper_uart_write_reg(uint8_t slave, uint8_t reg, uint32_t val);
esp_err_t stepper_uart_ensure_gconf_uart_mode(uint8_t slave);

esp_err_t stepper_driver_uart_init(void);
esp_err_t stepper_driver_ping(void);
esp_err_t stepper_driver_read_ifcnt(uint8_t *out);
esp_err_t stepper_driver_set_stealthchop(bool enable);
esp_err_t stepper_driver_set_microsteps(uint16_t microsteps);
esp_err_t stepper_driver_set_current(uint8_t run, uint8_t hold, uint8_t hold_delay);
esp_err_t stepper_driver_clear_faults(void);
bool stepper_driver_get_status_json(char *buf, size_t len);
