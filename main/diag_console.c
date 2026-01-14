#include <stdio.h>
#include "diag_console.h"

#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

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

    int64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t heap_min_free = esp_get_minimum_free_heap_size();
    const char *rr = reset_reason_to_str(esp_reset_reason());

    printf("{\"uptime_ms\":%lld,\"heap_free_bytes\":%u,\"heap_min_free_bytes\":%u,\"reset_reason\":\"%s\"}\n",
           (long long)uptime_ms, (unsigned)heap_free, (unsigned)heap_min_free, rr);
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

static void register_commands(void)
{
    const esp_console_cmd_t uptime_cmd = {
        .command = "uptime",
        .help = "Print uptime in ms",
        .hint = NULL,
        .func = &cmd_uptime,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&uptime_cmd));

    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Restart the device",
        .hint = NULL,
        .func = &cmd_reboot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));
    const esp_console_cmd_t snapshot_cmd = {
        .command = "snapshot",
        .help = "Print one-line JSON system snapshot",
        .hint = NULL,
        .func = &cmd_snapshot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&snapshot_cmd));
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

    // Built-in help command (lists all registered commands)
    ESP_ERROR_CHECK(esp_console_register_help_command());

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
