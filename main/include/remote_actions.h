#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    REMOTE_ACTION_OK = 0,
    REMOTE_ACTION_ERR_NOT_ALLOWED,
    REMOTE_ACTION_ERR_INVALID_ARGS,
    REMOTE_ACTION_ERR_UNLOCK_REQUIRED,
    REMOTE_ACTION_ERR_UNSAFE,
    REMOTE_ACTION_ERR_INTERNAL,
} remote_action_result_t;

size_t remote_actions_get_allowed(const char **names, size_t max_names);
bool remote_actions_is_unlocked_now(void);
void remote_actions_unlock(uint32_t seconds);
void remote_actions_lock(void);
void remote_actions_get_unlock_status(bool *unlocked, uint32_t *expires_in_s);

remote_action_result_t remote_actions_execute(const char *action,
                                             const char *args,
                                             char *out_json,
                                             size_t out_json_len);
