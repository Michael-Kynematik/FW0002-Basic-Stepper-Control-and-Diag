#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define MOTOR_MIN_HZ 50
#define MOTOR_MAX_HZ 5000

typedef enum
{
    MOTOR_STATE_DISABLED = 0,
    MOTOR_STATE_ENABLED_IDLE,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_FAULT,
} motor_state_t;

typedef enum
{
    MOTOR_DIR_FWD = 0,
    MOTOR_DIR_REV,
} motor_dir_t;

esp_err_t motor_init(void);
esp_err_t motor_enable(void);
esp_err_t motor_disable(void);
esp_err_t motor_set_dir(motor_dir_t dir);
esp_err_t motor_set_speed_hz(uint32_t step_hz);
esp_err_t motor_start(void);
esp_err_t motor_stop(void);
esp_err_t motor_clear_faults(void);
bool motor_get_status_json(char *buf, size_t len);
