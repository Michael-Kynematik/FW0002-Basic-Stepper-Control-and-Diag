#include "remote_actions.h"
// Remote actions are an allowlisted control surface (not the full CLI).
// "Unlock" is a time-limited gate, not authentication.

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "events.h"
#include "snapshot.h"
#include "neopixel.h"
#include "board.h"
#include "esp_timer.h"

#ifndef REMOTE_ACTIONS_REBOOT_NEEDS_UNLOCK
#define REMOTE_ACTIONS_REBOOT_NEEDS_UNLOCK 1
#endif

typedef remote_action_result_t (*remote_action_handler_t)(const char *args,
                                                          char *out_json,
                                                          size_t out_json_len);

typedef struct
{
    const char *name;
    bool require_unlock;
    remote_action_handler_t handler;
} remote_action_def_t;

static bool s_neopixel_on = false;
static int64_t s_unlock_expires_us = 0;

static remote_action_result_t action_safe(const char *args, char *out_json, size_t out_len);
static remote_action_result_t action_reboot(const char *args, char *out_json, size_t out_len);
static remote_action_result_t action_snapshot_now(const char *args, char *out_json, size_t out_len);
static remote_action_result_t action_neopixel_status(const char *args, char *out_json, size_t out_len);
static remote_action_result_t action_neopixel_set(const char *args, char *out_json, size_t out_len);

static const remote_action_def_t k_actions[] = {
    {.name = "safe", .require_unlock = false, .handler = action_safe},
    {.name = "reboot", .require_unlock = (REMOTE_ACTIONS_REBOOT_NEEDS_UNLOCK != 0), .handler = action_reboot},
    {.name = "snapshot_now", .require_unlock = false, .handler = action_snapshot_now},
    {.name = "neopixel_status", .require_unlock = false, .handler = action_neopixel_status},
    {.name = "neopixel_set", .require_unlock = true, .handler = action_neopixel_set},
};

static bool is_motor_action(const char *action)
{
    if (action == NULL)
    {
        return false;
    }
    if (strstr(action, "motor") != NULL)
    {
        return true;
    }
    if (strcmp(action, "move") == 0 || strcmp(action, "motion") == 0)
    {
        return true;
    }
    if (strcmp(action, "enable") == 0 || strcmp(action, "start") == 0 ||
        strcmp(action, "stop") == 0 || strcmp(action, "speed") == 0 ||
        strcmp(action, "dir") == 0)
    {
        return true;
    }
    return false;
}

size_t remote_actions_get_allowed(const char **names, size_t max_names)
{
    size_t count = sizeof(k_actions) / sizeof(k_actions[0]);
    if (names == NULL || max_names == 0)
    {
        return count;
    }
    size_t take = count;
    if (take > max_names)
    {
        take = max_names;
    }
    for (size_t i = 0; i < take; ++i)
    {
        names[i] = k_actions[i].name;
    }
    return count;
}

bool remote_actions_is_unlocked_now(void)
{
    if (s_unlock_expires_us == 0)
    {
        return false;
    }
    int64_t now_us = esp_timer_get_time();
    if (now_us >= s_unlock_expires_us)
    {
        s_unlock_expires_us = 0;
        events_emit("remote_unlock", "remote", 0, "expired");
        return false;
    }
    return true;
}

void remote_actions_unlock(uint32_t seconds)
{
    int64_t now_us = esp_timer_get_time();
    s_unlock_expires_us = now_us + ((int64_t)seconds * 1000000);
    events_emit("remote_unlock", "remote", 1, "set");
}

void remote_actions_lock(void)
{
    if (s_unlock_expires_us != 0)
    {
        s_unlock_expires_us = 0;
        events_emit("remote_unlock", "remote", 0, "cleared");
    }
}

void remote_actions_get_unlock_status(bool *unlocked, uint32_t *expires_in_s)
{
    bool is_unlocked = remote_actions_is_unlocked_now();
    if (unlocked != NULL)
    {
        *unlocked = is_unlocked;
    }
    if (expires_in_s != NULL)
    {
        uint32_t remaining = 0;
        if (is_unlocked)
        {
            int64_t now_us = esp_timer_get_time();
            int64_t remaining_us = s_unlock_expires_us - now_us;
            if (remaining_us > 0)
            {
                remaining = (uint32_t)(remaining_us / 1000000);
            }
        }
        *expires_in_s = remaining;
    }
}

remote_action_result_t remote_actions_execute(const char *action,
                                             const char *args,
                                             char *out_json,
                                             size_t out_json_len)
{
    if (out_json != NULL && out_json_len > 0)
    {
        out_json[0] = '\0';
    }
    if (action == NULL || action[0] == '\0')
    {
        return REMOTE_ACTION_ERR_INVALID_ARGS;
    }
    if (is_motor_action(action))
    {
        return REMOTE_ACTION_ERR_NOT_ALLOWED;
    }

    for (size_t i = 0; i < sizeof(k_actions) / sizeof(k_actions[0]); ++i)
    {
        if (strcmp(k_actions[i].name, action) == 0)
        {
            if (k_actions[i].require_unlock && !remote_actions_is_unlocked_now())
            {
                return REMOTE_ACTION_ERR_UNLOCK_REQUIRED;
            }
            return k_actions[i].handler(args, out_json, out_json_len);
        }
    }
    return REMOTE_ACTION_ERR_NOT_ALLOWED;
}

static remote_action_result_t action_safe(const char *args, char *out_json, size_t out_len)
{
    (void)args;
    (void)out_json;
    (void)out_len;
    board_safe();
    return REMOTE_ACTION_OK;
}

// Stub: returns OK but does not actually reboot yet.
static remote_action_result_t action_reboot(const char *args, char *out_json, size_t out_len)
{
    (void)args;
    (void)out_json;
    (void)out_len;
    return REMOTE_ACTION_OK;
}

static remote_action_result_t action_snapshot_now(const char *args, char *out_json, size_t out_len)
{
    (void)args;
    if (out_json == NULL || out_len == 0)
    {
        return REMOTE_ACTION_ERR_INTERNAL;
    }
    if (!snapshot_build(out_json, out_len))
    {
        out_json[0] = '\0';
        return REMOTE_ACTION_ERR_INTERNAL;
    }
    return REMOTE_ACTION_OK;
}

static remote_action_result_t action_neopixel_status(const char *args, char *out_json, size_t out_len)
{
    (void)args;
    if (out_json == NULL || out_len == 0)
    {
        return REMOTE_ACTION_ERR_INTERNAL;
    }
    int written = snprintf(out_json, out_len, "{\"neopixel_on\":%s}",
                           s_neopixel_on ? "true" : "false");
    if (written < 0 || (size_t)written >= out_len)
    {
        out_json[0] = '\0';
        return REMOTE_ACTION_ERR_INTERNAL;
    }
    return REMOTE_ACTION_OK;
}

static remote_action_result_t action_neopixel_set(const char *args, char *out_json, size_t out_len)
{
    (void)out_json;
    (void)out_len;
    if (args == NULL)
    {
        return REMOTE_ACTION_ERR_INVALID_ARGS;
    }
    if (strcmp(args, "off") == 0)
    {
        s_neopixel_on = false;
        neopixel_set_mode(NEOPIXEL_MODE_OFF);
        return REMOTE_ACTION_OK;
    }
    if (strcmp(args, "r") == 0)
    {
        s_neopixel_on = true;
        neopixel_set_rgb(255, 0, 0);
        return REMOTE_ACTION_OK;
    }
    if (strcmp(args, "g") == 0)
    {
        s_neopixel_on = true;
        neopixel_set_rgb(0, 255, 0);
        return REMOTE_ACTION_OK;
    }
    if (strcmp(args, "b") == 0)
    {
        s_neopixel_on = true;
        neopixel_set_rgb(0, 0, 255);
        return REMOTE_ACTION_OK;
    }
    return REMOTE_ACTION_ERR_INVALID_ARGS;
}
