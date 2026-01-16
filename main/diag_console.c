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
#include "motor.h"
#include "stepper_driver_uart.h"
#include "neopixel.h"
#include "ir_emitter.h"
#include "loadcell_scale.h"
#include "ir_sensor.h"

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
static bool s_cmd_neopixel_registered = false;
static bool s_cmd_ir_emitter_registered = false;
static bool s_cmd_ir_sensor_registered = false;
static bool s_cmd_scale_registered = false;
static bool s_cmd_motor_registered = false;

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
static int cmd_neopixel(int argc, char **argv);
static int cmd_ir_emitter(int argc, char **argv);
static int cmd_ir_sensor(int argc, char **argv);
static int cmd_scale(int argc, char **argv);
static int cmd_motor(int argc, char **argv);

static const diag_cmd_info_t k_diag_cmds[] = {
    {.name = "help", .usage = "[command]", .handler = &cmd_help, .registered = &s_cmd_help_registered},
    {.name = "uptime", .usage = "Print uptime in ms", .handler = &cmd_uptime, .registered = &s_cmd_uptime_registered},
    {.name = "reboot", .usage = "Restart the device", .handler = &cmd_reboot, .registered = &s_cmd_reboot_registered},
    {.name = "snapshot", .usage = "Print one-line JSON system snapshot", .handler = &cmd_snapshot, .registered = &s_cmd_snapshot_registered},
    {.name = "version", .usage = "Print firmware version/build", .handler = &cmd_version, .registered = &s_cmd_version_registered},
    {.name = "id", .usage = "Print device ID", .handler = &cmd_id, .registered = &s_cmd_id_registered},
    {.name = "pins", .usage = "Print pin map", .handler = &cmd_pins, .registered = &s_cmd_pins_registered},
    {.name = "safe", .usage = "Apply board safe state", .handler = &cmd_safe, .registered = &s_cmd_safe_registered},
    {.name = "neopixel", .usage = "off|r|g|b|booting|ready|fault|status|bright <0-255>", .handler = &cmd_neopixel, .registered = &s_cmd_neopixel_registered},
    {.name = "ir_emitter", .usage = "on|off|status", .handler = &cmd_ir_emitter, .registered = &s_cmd_ir_emitter_registered},
    {.name = "ir_sensor", .usage = "status", .handler = &cmd_ir_sensor, .registered = &s_cmd_ir_sensor_registered},
    {.name = "scale", .usage = "read [n] | tare [n] | cal <known_grams> [n] | status", .handler = &cmd_scale, .registered = &s_cmd_scale_registered},
    {.name = "motor", .usage = "enable|disable|dir CW|CCW|speed <hz 50-5000>|start|stop|status|clearfaults|driver ...", .handler = &cmd_motor, .registered = &s_cmd_motor_registered},
    {.name = "selftest", .usage = "Verify required commands and snapshot format", .handler = &cmd_selftest, .registered = &s_cmd_selftest_registered},
    {.name = "events", .usage = "tail [n] | clear", .handler = &cmd_events, .registered = &s_cmd_events_registered},
    {.name = "remote", .usage = "list | exec <action> [args...] | unlock <seconds> | lock | unlock_status", .handler = &cmd_remote, .registered = &s_cmd_remote_registered},
};

#define SNAPSHOT_JSON_MAX 512
#define SCALE_DEFAULT_SAMPLES 5
#define SCALE_MAX_SAMPLES 64

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

static void print_err_json(const char *err)
{
    if (err == NULL || err[0] == '\0')
    {
        err = "error";
    }
    printf("ERR {\"err\":\"%s\"}\n", err);
}

static bool parse_samples_arg(const char *arg, int *samples)
{
    if (samples == NULL || arg == NULL)
    {
        return false;
    }
    char *end = NULL;
    long val = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || val <= 0 || val > SCALE_MAX_SAMPLES)
    {
        return false;
    }
    *samples = (int)val;
    return true;
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
    if (argc == 3 && strcmp(argv[1], "motor") == 0 && strcmp(argv[2], "driver") == 0)
    {
        printf("motor driver ping | ifcnt | stealthchop on|off | microsteps <1|2|4|8|16|32|64|128|256> | current run <0-31> hold <0-31> [hold_delay <0-15>] | status | clearfaults\n");
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
    printf("{\"neopixel_onboard\":%d,\"ir_emitter\":%d,\"ir_sensor_input\":%d,"
           "\"loadcell_adc_sck\":%d,\"loadcell_adc_dout\":%d,"
           "\"stepper_driver_step\":%d,\"stepper_driver_dir\":%d,"
           "\"stepper_driver_en\":%d,\"stepper_driver_diag\":%d,"
           "\"stepper_driver_uart_tx\":%d,\"stepper_driver_uart_rx\":%d}\n",
           PIN_NEOPIXEL_ONBOARD, PIN_IR_EMITTER, PIN_IR_SENSOR_INPUT,
           PIN_LOADCELL_ADC_SCK, PIN_LOADCELL_ADC_DOUT,
           PIN_STEPPER_DRIVER_STEP, PIN_STEPPER_DRIVER_DIR,
           PIN_STEPPER_DRIVER_EN, PIN_STEPPER_DRIVER_DIAG,
           PIN_STEPPER_DRIVER_UART_TX, PIN_STEPPER_DRIVER_UART_RX);
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

static int cmd_neopixel(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        if (argc != 2)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        char buf[128];
        if (!neopixel_get_status_json(buf, sizeof(buf)))
        {
            printf("ERR neopixel\n");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "bright") == 0)
    {
        if (argc != 3)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        char *end = NULL;
        long val = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || val < 0 || val > 255)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
        neopixel_set_brightness((uint8_t)val);
        printf("OK\n");
        return 0;
    }
    if (argc != 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        neopixel_set_mode(NEOPIXEL_MODE_OFF);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "r") == 0)
    {
        neopixel_set_rgb(255, 0, 0);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "g") == 0)
    {
        neopixel_set_rgb(0, 255, 0);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "b") == 0)
    {
        neopixel_set_rgb(0, 0, 255);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "booting") == 0)
    {
        neopixel_set_mode(NEOPIXEL_MODE_BOOTING);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "ready") == 0)
    {
        neopixel_set_mode(NEOPIXEL_MODE_READY);
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "fault") == 0)
    {
        neopixel_set_mode(NEOPIXEL_MODE_FAULT);
        printf("OK\n");
        return 0;
    }
    printf("ERR invalid_args\n");
    return 0;
}

static int cmd_ir_emitter(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        char buf[32];
        if (!ir_emitter_get_status_json(buf, sizeof(buf)))
        {
            printf("ERR ir_emitter\n");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "on") == 0)
    {
        if (!ir_emitter_set(true))
        {
            printf("ERR ir_emitter\n");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        if (!ir_emitter_set(false))
        {
            printf("ERR ir_emitter\n");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    printf("ERR invalid_args\n");
    return 0;
}

static int cmd_ir_sensor(int argc, char **argv)
{
    if (argc == 2)
    {
        if (strcmp(argv[1], "status") != 0)
        {
            printf("ERR invalid_args\n");
            return 0;
        }
    }
    else if (argc != 1)
    {
        printf("ERR invalid_args\n");
        return 0;
    }
    char buf[32];
    if (!ir_sensor_get_status_json(buf, sizeof(buf)))
    {
        printf("ERR ir_sensor\n");
        return 0;
    }
    printf("%s\n", buf);
    return 0;
}

static int cmd_scale(int argc, char **argv)
{
    if (argc < 2)
    {
        print_err_json("invalid_args");
        return 0;
    }
    if (strcmp(argv[1], "read") == 0)
    {
        int samples = SCALE_DEFAULT_SAMPLES;
        if (argc == 3)
        {
            if (!parse_samples_arg(argv[2], &samples))
            {
                print_err_json("invalid_args");
                return 0;
            }
        }
        else if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        int32_t raw = 0;
        esp_err_t err = loadcell_scale_read_raw(samples, &raw);
        if (err != ESP_OK)
        {
            print_err_json("no_data");
            return 0;
        }
        bool calibrated = loadcell_scale_is_calibrated();
        char grams_buf[32];
        const char *grams_str = "null";
        if (calibrated)
        {
            float grams = 0.0f;
            if (loadcell_scale_raw_to_grams(raw, &grams) == ESP_OK)
            {
                snprintf(grams_buf, sizeof(grams_buf), "%.3f", (double)grams);
                grams_str = grams_buf;
            }
            else
            {
                calibrated = false;
            }
        }
        printf("{\"raw\":%ld,\"grams\":%s,\"samples\":%d,\"calibrated\":%s}\n",
               (long)raw, grams_str, samples, calibrated ? "true" : "false");
        return 0;
    }
    if (strcmp(argv[1], "tare") == 0)
    {
        int samples = SCALE_DEFAULT_SAMPLES;
        if (argc == 3)
        {
            if (!parse_samples_arg(argv[2], &samples))
            {
                print_err_json("invalid_args");
                return 0;
            }
        }
        else if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = loadcell_scale_tare(samples);
        if (err != ESP_OK)
        {
            print_err_json(err == ESP_ERR_INVALID_ARG ? "invalid_args" : "no_data");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "cal") == 0)
    {
        if (argc < 3 || argc > 4)
        {
            print_err_json("invalid_args");
            return 0;
        }
        char *end = NULL;
        float known_grams = strtof(argv[2], &end);
        if (end == argv[2] || *end != '\0' || !(known_grams > 0.0f))
        {
            print_err_json("invalid_args");
            return 0;
        }
        int samples = SCALE_DEFAULT_SAMPLES;
        if (argc == 4 && !parse_samples_arg(argv[3], &samples))
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = loadcell_scale_calibrate(samples, known_grams);
        if (err != ESP_OK)
        {
            print_err_json(err == ESP_ERR_INVALID_ARG ? "invalid_args" : "no_data");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        char buf[160];
        if (!loadcell_scale_get_status_json(buf, sizeof(buf)))
        {
            print_err_json("internal");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    print_err_json("invalid_args");
    return 0;
}

static int cmd_motor(int argc, char **argv)
{
    if (argc < 2)
    {
        print_err_json("invalid_args");
        return 0;
    }
    if (strcmp(argv[1], "driver") == 0)
    {
        if (argc < 3)
        {
            print_err_json("invalid_args");
            return 0;
        }
        const char *sub = argv[2];
        if (strcmp(sub, "ping") == 0)
        {
            if (argc != 3)
            {
                print_err_json("invalid_args");
                return 0;
            }
            esp_err_t err = stepper_driver_ping();
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("OK\n");
            return 0;
        }
        if (strcmp(sub, "ifcnt") == 0)
        {
            if (argc != 3)
            {
                print_err_json("invalid_args");
                return 0;
            }
            uint8_t ifcnt = 0;
            esp_err_t err = stepper_driver_read_ifcnt(&ifcnt);
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("{\"ifcnt\":%u}\n", (unsigned)ifcnt);
            return 0;
        }
        if (strcmp(sub, "stealthchop") == 0)
        {
            if (argc != 4)
            {
                print_err_json("invalid_args");
                return 0;
            }
            bool enable = false;
            if (strcmp(argv[3], "on") == 0)
            {
                enable = true;
            }
            else if (strcmp(argv[3], "off") == 0)
            {
                enable = false;
            }
            else
            {
                print_err_json("invalid_args");
                return 0;
            }
            esp_err_t err = stepper_driver_set_stealthchop(enable);
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("OK\n");
            return 0;
        }
        if (strcmp(sub, "microsteps") == 0)
        {
            if (argc != 4)
            {
                print_err_json("invalid_args");
                return 0;
            }
            char *end = NULL;
            long micro = strtol(argv[3], &end, 10);
            if (end == argv[3] || *end != '\0')
            {
                print_err_json("invalid_args");
                return 0;
            }
            esp_err_t err = stepper_driver_set_microsteps((uint16_t)micro);
            if (err == ESP_ERR_INVALID_ARG)
            {
                print_err_json("invalid_args");
                return 0;
            }
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("OK\n");
            return 0;
        }
        if (strcmp(sub, "current") == 0)
        {
            if (argc < 7 || argc > 9)
            {
                print_err_json("invalid_args");
                return 0;
            }
            if (strcmp(argv[3], "run") != 0 || strcmp(argv[5], "hold") != 0)
            {
                print_err_json("invalid_args");
                return 0;
            }
            char *end = NULL;
            long run = strtol(argv[4], &end, 10);
            if (end == argv[4] || *end != '\0')
            {
                print_err_json("invalid_args");
                return 0;
            }
            end = NULL;
            long hold = strtol(argv[6], &end, 10);
            if (end == argv[6] || *end != '\0')
            {
                print_err_json("invalid_args");
                return 0;
            }
            long hold_delay = 0;
            if (argc == 9)
            {
                if (strcmp(argv[7], "hold_delay") != 0)
                {
                    print_err_json("invalid_args");
                    return 0;
                }
                end = NULL;
                hold_delay = strtol(argv[8], &end, 10);
                if (end == argv[8] || *end != '\0')
                {
                    print_err_json("invalid_args");
                    return 0;
                }
            }
            esp_err_t err = stepper_driver_set_current((uint8_t)run, (uint8_t)hold, (uint8_t)hold_delay);
            if (err == ESP_ERR_INVALID_ARG)
            {
                print_err_json("invalid_args");
                return 0;
            }
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("OK\n");
            return 0;
        }
        if (strcmp(sub, "status") == 0)
        {
            if (argc != 3)
            {
                print_err_json("invalid_args");
                return 0;
            }
            char buf[256];
            if (!stepper_driver_get_status_json(buf, sizeof(buf)))
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("%s\n", buf);
            return 0;
        }
        if (strcmp(sub, "clearfaults") == 0)
        {
            if (argc != 3)
            {
                print_err_json("invalid_args");
                return 0;
            }
            esp_err_t err = stepper_driver_clear_faults();
            if (err != ESP_OK)
            {
                print_err_json("uart_no_response");
                return 0;
            }
            printf("OK\n");
            return 0;
        }
        print_err_json("invalid_args");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        char buf[256];
        if (!motor_get_status_json(buf, sizeof(buf)))
        {
            print_err_json("motor");
            return 0;
        }
        printf("%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "enable") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_enable();
        if (err != ESP_OK)
        {
            print_err_json("motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "disable") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_disable();
        if (err != ESP_OK)
        {
            print_err_json("motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "dir") == 0)
    {
        if (argc != 3)
        {
            print_err_json("invalid_args");
            return 0;
        }
        motor_dir_t dir = MOTOR_DIR_FWD;
        if (strcmp(argv[2], "CW") == 0)
        {
            dir = MOTOR_DIR_FWD;
        }
        else if (strcmp(argv[2], "CCW") == 0)
        {
            dir = MOTOR_DIR_REV;
        }
        else
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_set_dir(dir);
        if (err != ESP_OK)
        {
            print_err_json("motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "speed") == 0)
    {
        if (argc != 3)
        {
            print_err_json("invalid_args");
            return 0;
        }
        char *end = NULL;
        long hz = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || hz < MOTOR_MIN_HZ || hz > MOTOR_MAX_HZ)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_set_speed_hz((uint32_t)hz);
        if (err != ESP_OK)
        {
            print_err_json(err == ESP_ERR_INVALID_ARG ? "invalid_args" : "motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "start") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_start();
        if (err != ESP_OK)
        {
            print_err_json(err == ESP_ERR_INVALID_STATE ? "not_enabled" : "motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "stop") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_stop();
        if (err != ESP_OK)
        {
            print_err_json("motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    if (strcmp(argv[1], "clearfaults") == 0)
    {
        if (argc != 2)
        {
            print_err_json("invalid_args");
            return 0;
        }
        esp_err_t err = motor_clear_faults();
        if (err != ESP_OK)
        {
            print_err_json("motor");
            return 0;
        }
        printf("OK\n");
        return 0;
    }
    print_err_json("invalid_args");
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
            const char *name = names[i];
            if (strcmp(name, "neopixel_set") == 0)
            {
                print_json_string("neopixel_set off|r|g|b");
            }
            else
            {
                print_json_string(name);
            }
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

    neopixel_set_mode(NEOPIXEL_MODE_READY);
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

