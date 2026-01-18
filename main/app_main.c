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
#include "reset_reason.h"

#include "esp_system.h"

void app_main(void)
{
    // Must run first: prevents any unintended motor twitch before the console/monitor attaches.
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
    // Boot-time motor canary: only run when explicitly enabled.
#if defined(CONFIG_FW_BOOT_ACCEPTANCETEST_ON_BOOT) && CONFIG_FW_BOOT_ACCEPTANCETEST_ON_BOOT
    diag_console_run_startup_acceptancetest();
#endif
    diag_console_start();
}
