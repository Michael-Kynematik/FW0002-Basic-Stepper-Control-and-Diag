#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVENTS_TYPE_MAX 16
#define EVENTS_SUBSYSTEM_MAX 16
#define EVENTS_REASON_MAX 48

typedef struct
{
    uint32_t id;
    int64_t ts_ms;
    char type[EVENTS_TYPE_MAX];
    char subsystem[EVENTS_SUBSYSTEM_MAX];
    int code;
    char reason[EVENTS_REASON_MAX];
} events_record_t;

typedef void (*events_iter_cb_t)(const events_record_t *rec, void *ctx);

void events_init(void);
bool events_emit(const char *type, const char *subsystem, int code, const char *reason);
bool events_clear(void);
void events_tail(size_t n, events_iter_cb_t cb, void *ctx);
