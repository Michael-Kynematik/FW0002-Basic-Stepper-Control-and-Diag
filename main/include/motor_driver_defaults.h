#pragma once

#include <stdbool.h>
#include <stdint.h>

// Single source of truth for motor-driver default settings.
typedef struct
{
    uint8_t run_current;
    uint8_t hold_current;
    uint8_t hold_delay;
    uint16_t microsteps;
    bool stealthchop;
} motor_driver_defaults_t;

#define MOTOR_DRIVER_DEFAULT_RUN_CURRENT 8
#define MOTOR_DRIVER_DEFAULT_HOLD_CURRENT 2
#define MOTOR_DRIVER_DEFAULT_HOLD_DELAY 8
#define MOTOR_DRIVER_DEFAULT_MICROSTEPS 16
#define MOTOR_DRIVER_DEFAULT_STEALTHCHOP true

static inline motor_driver_defaults_t motor_driver_defaults(void)
{
    return (motor_driver_defaults_t){
        .run_current = MOTOR_DRIVER_DEFAULT_RUN_CURRENT,
        .hold_current = MOTOR_DRIVER_DEFAULT_HOLD_CURRENT,
        .hold_delay = MOTOR_DRIVER_DEFAULT_HOLD_DELAY,
        .microsteps = MOTOR_DRIVER_DEFAULT_MICROSTEPS,
        .stealthchop = MOTOR_DRIVER_DEFAULT_STEALTHCHOP,
    };
}
