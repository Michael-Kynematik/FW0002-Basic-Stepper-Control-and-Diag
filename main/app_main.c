#include <stdio.h>
#include "diag_console.h"
#include "events.h"
#include "board.h"
#include "fw_version.h"
#include "motor.h"
#include "neopixel.h"
#include "ir_emitter.h"
#include "ir_sensor.h"
#include "loadcell_scale.h"

#include "esp_system.h"

static const char *reset_reason_to_str(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_UNKNOWN:
        return "UNKNOWN";
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "OTHER";
    }
}

void app_main(void)
{
    board_force_motor_pins_safe_early();
    printf("\nFW0002 boot v%s (%s)\n", FW_VERSION, FW_BUILD);
    events_init();
    events_emit("boot_reset", "system", (int)esp_reset_reason(), reset_reason_to_str(esp_reset_reason()));
    board_init_safe();
    motor_init();
    neopixel_init();
    ir_emitter_init();
    ir_sensor_init();
    loadcell_scale_init();
    neopixel_set_mode(NEOPIXEL_MODE_BOOTING);
    diag_console_run_startup_acceptancetest();
    diag_console_start();
}
