#include "board.h"

#include "driver/gpio.h"

#include "events.h"
#include "motor.h"

// Safe state (today): motor disabled + software flag for snapshot reporting.
// TODO: does not yet drive all peripheral GPIOs to safe defaults.
static bool s_safe_state = false;

// Early motor pin safing to prevent STEP/EN glitches right after reset/flash.
// Forces STEP low, EN disabled (per motor_disable polarity), DIR default before other init.
void board_force_motor_pins_safe_early(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_STEPPER_DRIVER_STEP) |
                        (1ULL << PIN_STEPPER_DRIVER_DIR) |
                        (1ULL << PIN_STEPPER_DRIVER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(PIN_STEPPER_DRIVER_STEP, 0);
    gpio_set_level(PIN_STEPPER_DRIVER_DIR, 0);
    gpio_set_level(PIN_STEPPER_DRIVER_EN, 1);
}

void board_init_safe(void)
{
    // TODO: drive actual GPIOs to safe defaults once wired.
    s_safe_state = true;
    events_emit("safe_state", "board", 0, "applied");
}

// Marks board safe and disables motor; TODO: safe all peripherals (GPIO defaults) later.
void board_safe(void)
{
    // TODO: drive actual GPIOs to safe defaults once wired.
    motor_disable();
    s_safe_state = true;
    events_emit("safe_state", "board", 0, "applied");
}

bool board_is_safe(void)
{
    return s_safe_state;
}
