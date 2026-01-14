#include <stdio.h>
#include "diag_console.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "events.h"

typedef struct
{
    const char *name;
    const char *usage;
    esp_console_cmd_func_t handler;
    bool *registered;
} diag_cmd_info_t;

static bool s_cmd_help_registered = false;
static bool s_cmd_uptime_registered = false;
static bool s_cmd_reboot_registered = false;
static bool s_cmd_snapshot_registered = false;
static bool s_cmd_selftest_registered = false;
static bool s_cmd_events_registered = false;

static int cmd_help(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_snapshot(int argc, char **argv);
static int cmd_selftest(int argc, char **argv);
static int cmd_events(int argc, char **argv);
static const char *reset_reason_to_str(esp_reset_reason_t r);

static const diag_cmd_info_t k_diag_cmds[] = {
    {.name = "help", .usage = "List commands", .handler = &cmd_help, .registered = &s_cmd_help_registered},
    {.name = "uptime", .usage = "Print uptime in ms", .handler = &cmd_uptime, .registered = &s_cmd_uptime_registered},
    {.name = "reboot", .usage = "Restart the device", .handler = &cmd_reboot, .registered = &s_cmd_reboot_registered},
    {.name = "snapshot", .usage = "Print one-line JSON system snapshot", .handler = &cmd_snapshot, .registered = &s_cmd_snapshot_registered},
    {.name = "selftest", .usage = "Verify required commands and snapshot format", .handler = &cmd_selftest, .registered = &s_cmd_selftest_registered},
    {.name = "events", .usage = "Event log (tail [n] | clear)", .handler = &cmd_events, .registered = &s_cmd_events_registered},
};

static void print_json_string(const char *value)
{
    if (value == NULL)
    {
        value = "";
    }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p)
    {
        unsigned char c = *p;
        if (c == '"' || c == '\\')
        {
            putchar('\\');
            putchar((char)c);
        }
        else if (c < 0x20)
        {
            printf("\\u%04x", (unsigned)c);
        }
        else
        {
            putchar((char)c);
        }
    }
    putchar('"');
}

static void events_print_record(const events_record_t *rec, void *ctx)
{
    (void)ctx;
    printf("{\"id\":%u,\"ts_ms\":%lld,\"type\":", (unsigned)rec->id, (long long)rec->ts_ms);
    print_json_string(rec->type);
    printf(",\"subsystem\":");
    print_json_string(rec->subsystem);
    printf(",\"code\":%d,\"reason\":", rec->code);
    print_json_string(rec->reason);
    printf("}\n");
}

static bool build_snapshot_json(char *buf, size_t len)
{
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t heap_min_free = esp_get_minimum_free_heap_size();
    const char *rr = reset_reason_to_str(esp_reset_reason());

    int written = snprintf(buf, len,
                           "{\"uptime_ms\":%lld,\"heap_free_bytes\":%u,\"heap_min_free_bytes\":%u,\"reset_reason\":\"%s\"}",
                           (long long)uptime_ms, (unsigned)heap_free, (unsigned)heap_min_free, rr);
    if (written < 0 || (size_t)written >= len)
    {
        return false;
    }
    return true;
}

static const diag_cmd_info_t *find_cmd_info(const char *name)
{
    for (size_t i = 0; i < sizeof(k_diag_cmds) / sizeof(k_diag_cmds[0]); ++i)
    {
        if (strcmp(k_diag_cmds[i].name, name) == 0)
        {
            return &k_diag_cmds[i];
        }
    }
    return NULL;
}

static bool is_cmd_registered(const char *name)
{
    const diag_cmd_info_t *info = find_cmd_info(name);
    if (info == NULL || info->registered == NULL)
    {
        return false;
    }
    return *info->registered;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Commands:\n");
    for (size_t i = 0; i < sizeof(k_diag_cmds) / sizeof(k_diag_cmds[0]); ++i)
    {
        printf("%-10s %s\n", k_diag_cmds[i].name, k_diag_cmds[i].usage);
    }
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int64_t us = esp_timer_get_time();
    printf("uptime_ms=%lld\n", (long long)(us / 1000));
    return 0;
}

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

static int cmd_snapshot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char buf[128];
    if (!build_snapshot_json(buf, sizeof(buf)))
    {
        printf("{\"error\":\"snapshot_format\"}\n");
        return 0;
    }
    printf("%s\n", buf);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("restarting...\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static int cmd_selftest(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!is_cmd_registered("help"))
    {
        printf("ERR missing_help\n");
        return 0;
    }
    if (!is_cmd_registered("uptime"))
    {
        printf("ERR missing_uptime\n");
        return 0;
    }
    if (!is_cmd_registered("reboot"))
    {
        printf("ERR missing_reboot\n");
        return 0;
    }
    if (!is_cmd_registered("snapshot"))
    {
        printf("ERR missing_snapshot\n");
        return 0;
    }

    char buf[128];
    if (!build_snapshot_json(buf, sizeof(buf)))
    {
        printf("ERR snapshot_format\n");
        return 0;
    }
    if (strpbrk(buf, "\r\n") != NULL)
    {
        printf("ERR snapshot_multiline\n");
        return 0;
    }
    size_t len = strlen(buf);
    if (len < 2 || buf[0] != '{' || buf[len - 1] != '}')
    {
        printf("ERR snapshot_format\n");
        return 0;
    }

    printf("OK\n");
    return 0;
}

static int cmd_events(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "tail") == 0)
    {
        size_t n = 10;
        if (argc == 3)
        {
            char *end = NULL;
            long val = strtol(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0' || val <= 0)
            {
                printf("ERR invalid_args\n");
                return 0;
            }
            n = (size_t)val;
        }
        else if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        events_tail(n, events_print_record, NULL);
        return 0;
    }
    if (strcmp(argv[1], "clear") == 0)
    {
        if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        if (!events_clear())
        {
            printf("ERR clear\n");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    printf("ERR invalid_args\n");
    return 0;
}

static void register_commands(void)
{
    for (size_t i = 0; i < sizeof(k_diag_cmds) / sizeof(k_diag_cmds[0]); ++i)
    {
        const esp_console_cmd_t cmd = {
            .command = k_diag_cmds[i].name,
            .help = k_diag_cmds[i].usage,
            .hint = NULL,
            .func = k_diag_cmds[i].handler,
        };
        esp_err_t err = esp_console_cmd_register(&cmd);
        if (k_diag_cmds[i].registered != NULL)
        {
            *k_diag_cmds[i].registered = (err == ESP_OK);
        }
    }
}

void diag_console_start(void)
{
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // idf.py monitor can strip ESC (0x1B), which breaks linenoise ANSI handling.
    // Dumb mode avoids ANSI cursor-position queries and keeps input stable.
    linenoiseSetMultiLine(0);
    linenoiseSetDumbMode(1);
    linenoiseHistorySetMaxLen(50);

    register_commands();

    printf("\nFW0002 diagnostic console\n");
    printf("Type 'help' to list commands.\n\n");

    while (true)
    {
        char *line = linenoise("fw0002> ");
        if (line == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (strlen(line) > 0)
        {
            linenoiseHistoryAdd(line);
            int ret = 0;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND)
            {
                printf("Unrecognized command\n");
            }
            else if (err == ESP_ERR_INVALID_ARG)
            {
                printf("Invalid arguments\n");
            }
            else if (err != ESP_OK)
            {
                printf("Command error: %s\n", esp_err_to_name(err));
            }
        }
        linenoiseFree(line);
    }
}
