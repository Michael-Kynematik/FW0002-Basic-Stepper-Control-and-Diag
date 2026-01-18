#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HW_REV 1

// Centralized pin map
#define PIN_NEOPIXEL_ONBOARD  48
#define PIN_IR_EMITTER        8
#define PIN_IR_SENSOR_INPUT   10
#define PIN_LOADCELL_ADC_SCK  12
#define PIN_LOADCELL_ADC_DOUT 13

#define PIN_STEPPER_DRIVER_STEP    4
#define PIN_STEPPER_DRIVER_DIR     5
#define PIN_STEPPER_DRIVER_EN      6
#define PIN_STEPPER_DRIVER_DIAG    7
#define PIN_STEPPER_DRIVER_UART_TX 17
#define PIN_STEPPER_DRIVER_UART_RX 18

// RESERVED / HIGH-RISK PINS (DevKitC-1 / ESP32-S3)
// USB CDC console: GPIO 19/20 (D-/D+) are reserved.
// Boot/strapping pins: GPIO 0, GPIO 45, GPIO 46 (avoid driving at reset).
// Onboard NeoPixel data pin is GPIO 48.
// Console invariant: USB CDC on known-good COM port; do not change transport without validation.

void board_init_safe(void);
void board_force_motor_pins_safe_early(void);
void board_safe(void);
bool board_is_safe(void);
