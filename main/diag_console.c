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
#include "snapshot.h"
#include "remote_actions.h"
#include "fw_version.h"
#include "board.h"
#include "led.h"
#include "ir.h"
#include "beam.h"

#include "esp_mac.h"

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
static bool s_cmd_remote_registered = false;
static bool s_cmd_version_registered = false;
static bool s_cmd_id_registered = false;
static bool s_cmd_pins_registered = false;
static bool s_cmd_safe_registered = false;
static bool s_cmd_led_registered = false;
static bool s_cmd_ir_registered = false;
static bool s_cmd_beam_registered = false;

static int cmd_help(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_snapshot(int argc, char **argv);
static int cmd_selftest(int argc, char **argv);
static int cmd_events(int argc, char **argv);
static int cmd_remote(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_id(int argc, char **argv);
static int cmd_pins(int argc, char **argv);
static int cmd_safe(int argc, char **argv);
static int cmd_led(int argc, char **argv);
static int cmd_ir(int argc, char **argv);
static int cmd_beam(int argc, char **argv);

static const diag_cmd_info_t k_diag_cmds[] = {
    {.name = "help", .usage = "[command]", .handler = &cmd_help, .registered = &s_cmd_help_registered},
    {.name = "uptime", .usage = "Print uptime in ms", .handler = &cmd_uptime, .registered = &s_cmd_uptime_registered},
    {.name = "reboot", .usage = "Restart the device", .handler = &cmd_reboot, .registered = &s_cmd_reboot_registered},
    {.name = "snapshot", .usage = "Print one-line JSON system snapshot", .handler = &cmd_snapshot, .registered = &s_cmd_snapshot_registered},
    {.name = "version", .usage = "Print firmware version/build", .handler = &cmd_version, .registered = &s_cmd_version_registered},
    {.name = "id", .usage = "Print device ID", .handler = &cmd_id, .registered = &s_cmd_id_registered},
    {.name = "pins", .usage = "Print pin map", .handler = &cmd_pins, .registered = &s_cmd_pins_registered},
    {.name = "safe", .usage = "Apply board safe state", .handler = &cmd_safe, .registered = &s_cmd_safe_registered},
    {.name = "led", .usage = "off|r|g|b|booting|ready|fault|status", .handler = &cmd_led, .registered = &s_cmd_led_registered},
    {.name = "ir", .usage = "on|off|status", .handler = &cmd_ir, .registered = &s_cmd_ir_registered},
    {.name = "beam", .usage = "status", .handler = &cmd_beam, .registered = &s_cmd_beam_registered},
    {.name = "selftest", .usage = "Verify required commands and snapshot format", .handler = &cmd_selftest, .registered = &s_cmd_selftest_registered},
    {.name = "events", .usage = "tail [n] | clear", .handler = &cmd_events, .registered = &s_cmd_events_registered},
    {.name = "remote", .usage = "list | exec <action> [args...] | unlock <seconds> | lock | unlock_status", .handler = &cmd_remote, .registered = &s_cmd_remote_registered},
};

#define SNAPSHOT_JSON_MAX 512

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
    if (argc == 1)
    {
        printf("Commands:\n");
        for (size_t i = 0; i < sizeof(k_diag_cmds) / sizeof(k_diag_cmds[0]); ++i)
        {
            printf("%-10s %s\n", k_diag_cmds[i].name, k_diag_cmds[i].usage);
        }
        return 0;
    }
    if (argc == 2)
    {
        const diag_cmd_info_t *info = find_cmd_info(argv[1]);
        if (info == NULL)
        {
            printf("ERR unknown_command\n");
            return 0;
        }
        printf("%s %s\n", info->name, info->usage);
        return 0;
    }
    printf("ERR invalid_args\n");
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

static int cmd_snapshot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char buf[SNAPSHOT_JSON_MAX];
    if (!snapshot_build(buf, sizeof(buf)))
    {
        printf("{\"error\":\"snapshot_format\"}\n");
        return 0;
    }
    printf("%s\n", buf);
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("{\"fw_version\":\"%s\",\"fw_build\":\"%s\"}\n", FW_VERSION, FW_BUILD);
    return 0;
}

static void format_mac_hex(const uint8_t mac[6], char *out, size_t out_len)
{
    if (out == NULL || out_len < 13)
    {
        return;
    }
    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int cmd_id(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    {
        printf("ERR read_mac\n");
        return 0;
    }
    char id[13];
    format_mac_hex(mac, id, sizeof(id));
    printf("{\"device_id\":\"%s\"}\n", id);
    return 0;
}

static int cmd_pins(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("{\"neopixel_onboard\":%d,\"ir_emitter\":%d,\"beam_input\":%d,"
           "\"hx711_sck\":%d,\"hx711_dout\":%d,"
           "\"tmc_step\":%d,\"tmc_dir\":%d,\"tmc_en\":%d,\"tmc_diag\":%d,"
           "\"tmc_uart_tx\":%d,\"tmc_uart_rx\":%d}\n",
           PIN_NEOPIXEL_ONBOARD, PIN_IR_EMITTER, PIN_BEAM_INPUT,
           PIN_HX711_SCK, PIN_HX711_DOUT,
           PIN_TMC_STEP, PIN_TMC_DIR, PIN_TMC_EN, PIN_TMC_DIAG,
           PIN_TMC_UART_TX, PIN_TMC_UART_RX);
    return 0;
}

static int cmd_safe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    board_safe();
    printf("OK\n");
    return 0;
}

static int cmd_led(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        char buf[96];
        if (!led_get_status_json(buf, sizeof(buf)))
        {
            printf("ERR led\n");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        led_set_mode(LED_MODE_OFF);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "r") == 0)
    {
        led_set_rgb(255, 0, 0);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "g") == 0)
    {
        led_set_rgb(0, 255, 0);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "b") == 0)
    {
        led_set_rgb(0, 0, 255);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "booting") == 0)
    {
        led_set_mode(LED_MODE_BOOTING);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "ready") == 0)
    {
        led_set_mode(LED_MODE_READY);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "fault") == 0)
    {
        led_set_mode(LED_MODE_FAULT);
        printf("OK\n");
        return 0;
    }
    printf("ERR invalid_args\n");
    return 0;
}

static int cmd_ir(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        char buf[32];
        if (!ir_get_status_json(buf, sizeof(buf)))
        {
            printf("ERR ir\n");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "on") == 0)
    {
        if (!ir_set(true))
        {
            printf("ERR ir\n");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        if (!ir_set(false))
        {
            printf("ERR ir\n");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    printf("ERR invalid_args\n");
    return 0;
}

static int cmd_beam(int argc, char **argv)
{
    if (argc != 1)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    char buf[32];
    if (!beam_get_status_json(buf, sizeof(buf)))
    {
        printf("ERR beam\n");
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

    char buf[SNAPSHOT_JSON_MAX];
    if (!snapshot_build(buf, sizeof(buf)))
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

static const char *remote_result_to_err(remote_action_result_t result)
{
    switch (result)
    {
    case REMOTE_ACTION_ERR_NOT_ALLOWED:
        return "not_allowed";
    case REMOTE_ACTION_ERR_INVALID_ARGS:
        return "invalid_args";
    case REMOTE_ACTION_ERR_UNLOCK_REQUIRED:
        return "unlock_required";
    case REMOTE_ACTION_ERR_UNSAFE:
        return "unsafe";
    case REMOTE_ACTION_ERR_INTERNAL:
    default:
        return "internal";
    }
}

static int cmd_remote(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "list") == 0)
    {
        const char *names[8];
        size_t count = remote_actions_get_allowed(names, sizeof(names) / sizeof(names[0]));
        size_t to_print = count;
        if (to_print > (sizeof(names) / sizeof(names[0])))
        {
            to_print = sizeof(names) / sizeof(names[0]);
        }
        printf("{\"actions\":[");
        for (size_t i = 0; i < to_print; ++i)
        {
            if (i > 0)
            {
                printf(",");
            }
            print_json_string(names[i]);
        }
        printf("]}\n");
        return 0;
    }
    if (strcmp(argv[1], "unlock") == 0)
    {
        uint32_t seconds = 60;
        if (argc == 3)
        {
            char *end = NULL;
            long val = strtol(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0')
            {
                printf("ERR invalid_args\n");
                return 0;
            }
            seconds = (uint32_t)val;
        }
        else if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        if (seconds < 10)
        {
            seconds = 10;
        }
        if (seconds > 600)
        {
            seconds = 600;
        }
        remote_actions_unlock(seconds);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "lock") == 0)
    {
        if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        remote_actions_lock();
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "unlock_status") == 0)
    {
        if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        bool unlocked = false;
        uint32_t expires_in_s = 0;
        remote_actions_get_unlock_status(&unlocked, &expires_in_s);
        printf("{\"unlocked\":%s,\"expires_in_s\":%u}\n",
               unlocked ? "true" : "false",
               (unsigned)expires_in_s);
        return 0;
    }
    if (strcmp(argv[1], "exec") == 0)
    {
        if (argc < 3)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        const char *args = NULL;
        char args_buf[96];
        if (argc > 3)
        {
            size_t used = 0;
            for (int i = 3; i < argc; ++i)
            {
                size_t part_len = strlen(argv[i]);
                if (used + part_len + 1 >= sizeof(args_buf))
                {
                    printf("ERR invalid_args\n");
                    return 0;
                }
                if (used > 0)
                {
                    args_buf[used++] = ' ';
                }
                memcpy(&args_buf[used], argv[i], part_len);
                used += part_len;
                args_buf[used] = '\0';
            }
            args = args_buf;
        }
        char out_json[160];
        remote_action_result_t result =
            remote_actions_execute(argv[2], args, out_json, sizeof(out_json));
        if (result != REMOTE_ACTION_OK)
        {
            if (result == REMOTE_ACTION_ERR_UNLOCK_REQUIRED)
            {
                printf("ERR {\"err\":\"unlock_required\"}\n");
            }
            else
            {
                printf("ERR %s\n", remote_result_to_err(result));
            }
            return 0;
        }
        if (out_json[0] != '\0')
        {
            printf("%s\n", out_json);
        }
        else
        {
            printf("OK\n");
        }
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

    led_set_mode(LED_MODE_READY);
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
