#include "events.h"

#include <string.h>

#include "esp_timer.h"

#define EVENTS_CAPACITY 64

static events_record_t s_events[EVENTS_CAPACITY];
static size_t s_head = 0;
static size_t s_count = 0;
static uint32_t s_next_id = 1;

static void events_copy_string(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0)
    {
        return;
    }
    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_len && src[i] != '\0'; ++i)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void events_init(void)
{
    s_head = 0;
    s_count = 0;
    s_next_id = 1;
    memset(s_events, 0, sizeof(s_events));
}

bool events_emit(const char *type, const char *subsystem, int code, const char *reason)
{
    events_record_t *rec = &s_events[s_head];
    rec->id = s_next_id++;
    rec->ts_ms = esp_timer_get_time() / 1000;
    rec->code = code;
    events_copy_string(rec->type, sizeof(rec->type), type);
    events_copy_string(rec->subsystem, sizeof(rec->subsystem), subsystem);
    events_copy_string(rec->reason, sizeof(rec->reason), reason);

    s_head = (s_head + 1) % EVENTS_CAPACITY;
    if (s_count < EVENTS_CAPACITY)
    {
        s_count++;
    }
    return true;
}

bool events_clear(void)
{
    s_head = 0;
    s_count = 0;
    memset(s_events, 0, sizeof(s_events));
    return true;
}

void events_tail(size_t n, events_iter_cb_t cb, void *ctx)
{
    if (cb == NULL)
    {
        return;
    }
    if (n == 0 || s_count == 0)
    {
        return;
    }
    size_t take = n;
    if (take > s_count)
    {
        take = s_count;
    }
    size_t oldest = (s_head + EVENTS_CAPACITY - s_count) % EVENTS_CAPACITY;
    size_t start = (oldest + (s_count - take)) % EVENTS_CAPACITY;
    for (size_t i = 0; i < take; ++i)
    {
        size_t idx = (start + i) % EVENTS_CAPACITY;
        cb(&s_events[idx], ctx);
    }
}
