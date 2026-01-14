#include "board.h"

#include "events.h"

static bool s_safe_state = false;

void board_init_safe(void)
{
    // TODO: drive actual GPIOs to safe defaults once wired.
    s_safe_state = true;
    events_emit("safe_state", "board", 0, "applied");
}

void board_safe(void)
{
    // TODO: drive actual GPIOs to safe defaults once wired.
    s_safe_state = true;
    events_emit("safe_state", "board", 0, "applied");
}

bool board_is_safe(void)
{
    return s_safe_state;
}
