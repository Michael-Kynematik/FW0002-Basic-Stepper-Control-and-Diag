#include "motor.h"

#include <string.h>
#include <stdio.h>

#include "board.h"
#include "events.h"
#include "stepper_driver_uart.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"

#define MOTOR_EN_ACTIVE_LEVEL 0
#define MOTOR_DIR_FWD_LEVEL 0
#define MOTOR_TIMER_RES_HZ 1000000

static gptimer_handle_t s_timer = NULL;
static bool s_timer_running = false;
static bool s_step_level = false;

static uint32_t s_step_hz = 0;
static motor_dir_t s_dir = MOTOR_DIR_FWD;
static bool s_enabled = false;
static motor_state_t s_state = MOTOR_STATE_DISABLED;
static int s_fault_code = 0;
static char s_fault_reason[32] = "none";

static const char *motor_state_to_str(motor_state_t state)
{
    switch (state)
    {
    case MOTOR_STATE_DISABLED:
        return "disabled";
    case MOTOR_STATE_ENABLED_IDLE:
        return "enabled_idle";
    case MOTOR_STATE_RUNNING:
        return "running";
    case MOTOR_STATE_FAULT:
        return "fault";
    default:
        return "unknown";
    }
}

static const char *motor_dir_to_str(motor_dir_t dir)
{
    return (dir == MOTOR_DIR_REV) ? "CCW" : "CW";
}

static void motor_set_step_level(bool level)
{
    s_step_level = level;
    gpio_set_level(PIN_STEPPER_DRIVER_STEP, level ? 1 : 0);
}

static bool IRAM_ATTR motor_on_alarm(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t *edata,
                                     void *user_data)
{
    (void)timer;
    (void)edata;
    (void)user_data;
    s_step_level = !s_step_level;
    gpio_set_level(PIN_STEPPER_DRIVER_STEP, s_step_level ? 1 : 0);
    return false;
}

static esp_err_t motor_config_timer(uint32_t step_hz)
{
    if (step_hz == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t half_period_us = 1000000ULL / (step_hz * 2ULL);
    if (half_period_us == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    gptimer_alarm_config_t alarm_cfg = {
        .reload_count = 0,
        .alarm_count = half_period_us,
        .flags = {
            .auto_reload_on_alarm = true,
        },
    };
    esp_err_t err = gptimer_set_alarm_action(s_timer, &alarm_cfg);
    if (err != ESP_OK)
    {
        return err;
    }
    return gptimer_set_raw_count(s_timer, 0);
}

esp_err_t motor_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_STEPPER_DRIVER_STEP) |
                        (1ULL << PIN_STEPPER_DRIVER_DIR) |
                        (1ULL << PIN_STEPPER_DRIVER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&out_cfg);
    if (err != ESP_OK)
    {
        return err;
    }
    gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << PIN_STEPPER_DRIVER_DIAG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&in_cfg);
    if (err != ESP_OK)
    {
        return err;
    }

    gpio_set_level(PIN_STEPPER_DRIVER_EN, !MOTOR_EN_ACTIVE_LEVEL);
    gpio_set_level(PIN_STEPPER_DRIVER_DIR, MOTOR_DIR_FWD_LEVEL);
    motor_set_step_level(false);

    gptimer_config_t timer_cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = MOTOR_TIMER_RES_HZ,
    };
    err = gptimer_new_timer(&timer_cfg, &s_timer);
    if (err != ESP_OK)
    {
        return err;
    }

    gptimer_event_callbacks_t cbs = {
        .on_alarm = motor_on_alarm,
    };
    err = gptimer_register_event_callbacks(s_timer, &cbs, NULL);
    if (err != ESP_OK)
    {
        return err;
    }
    err = gptimer_enable(s_timer);
    if (err != ESP_OK)
    {
        return err;
    }

    s_step_hz = 0;
    s_dir = MOTOR_DIR_FWD;
    s_enabled = false;
    s_state = MOTOR_STATE_DISABLED;
    s_fault_code = 0;
    snprintf(s_fault_reason, sizeof(s_fault_reason), "none");
    stepper_driver_uart_init();
    return ESP_OK;
}

esp_err_t motor_enable(void)
{
    if (s_enabled)
    {
        return ESP_OK;
    }
    gpio_set_level(PIN_STEPPER_DRIVER_EN, MOTOR_EN_ACTIVE_LEVEL);
    s_enabled = true;
    if (s_state != MOTOR_STATE_FAULT)
    {
        s_state = MOTOR_STATE_ENABLED_IDLE;
    }
    events_emit("motor_enable", "motor", 0, "enabled");
    return ESP_OK;
}

esp_err_t motor_disable(void)
{
    motor_stop();
    if (!s_enabled)
    {
        s_step_hz = 0;
        s_state = MOTOR_STATE_DISABLED;
        return ESP_OK;
    }
    gpio_set_level(PIN_STEPPER_DRIVER_EN, !MOTOR_EN_ACTIVE_LEVEL);
    s_enabled = false;
    s_state = MOTOR_STATE_DISABLED;
    s_step_hz = 0;
    events_emit("motor_enable", "motor", 0, "disabled");
    return ESP_OK;
}

esp_err_t motor_set_dir(motor_dir_t dir)
{
    s_dir = (dir == MOTOR_DIR_REV) ? MOTOR_DIR_REV : MOTOR_DIR_FWD;
    gpio_set_level(PIN_STEPPER_DRIVER_DIR,
                   (s_dir == MOTOR_DIR_FWD) ? MOTOR_DIR_FWD_LEVEL : !MOTOR_DIR_FWD_LEVEL);
    events_emit("motor_dir", "motor", 0, motor_dir_to_str(s_dir));
    return ESP_OK;
}

esp_err_t motor_set_speed_hz(uint32_t step_hz)
{
    if (step_hz < MOTOR_MIN_HZ || step_hz > MOTOR_MAX_HZ)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_step_hz = step_hz;
    if (s_state == MOTOR_STATE_RUNNING)
    {
        gptimer_stop(s_timer);
        s_timer_running = false;
        motor_set_step_level(false);
        esp_err_t err = motor_config_timer(s_step_hz);
        if (err != ESP_OK)
        {
            return err;
        }
        err = gptimer_start(s_timer);
        if (err != ESP_OK)
        {
            return err;
        }
        s_timer_running = true;
    }
    char reason[EVENTS_REASON_MAX];
    int written = snprintf(reason, sizeof(reason), "%uHz", (unsigned)s_step_hz);
    if (written < 0)
    {
        reason[0] = '\0';
    }
    events_emit("motor_speed", "motor", 0, reason);
    return ESP_OK;
}

esp_err_t motor_start(void)
{
    if (!s_enabled)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == MOTOR_STATE_FAULT)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_step_hz == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = motor_config_timer(s_step_hz);
    if (err != ESP_OK)
    {
        return err;
    }
    motor_set_step_level(false);
    err = gptimer_start(s_timer);
    if (err != ESP_OK)
    {
        return err;
    }
    s_timer_running = true;
    s_state = MOTOR_STATE_RUNNING;
    char reason[EVENTS_REASON_MAX];
    int written = snprintf(reason, sizeof(reason), "%uHz %s",
                           (unsigned)s_step_hz, motor_dir_to_str(s_dir));
    if (written < 0)
    {
        reason[0] = '\0';
    }
    events_emit("motor_start", "motor", 0, reason);
    return ESP_OK;
}

esp_err_t motor_stop(void)
{
    bool was_running = (s_state == MOTOR_STATE_RUNNING) || s_timer_running;
    if (s_timer_running)
    {
        gptimer_stop(s_timer);
        s_timer_running = false;
    }
    motor_set_step_level(false);
    if (s_state != MOTOR_STATE_FAULT)
    {
        s_state = s_enabled ? MOTOR_STATE_ENABLED_IDLE : MOTOR_STATE_DISABLED;
    }
    if (was_running)
    {
        events_emit("motor_stop", "motor", 0, "stopped");
    }
    return ESP_OK;
}

esp_err_t motor_clear_faults(void)
{
    s_fault_code = 0;
    snprintf(s_fault_reason, sizeof(s_fault_reason), "none");
    s_state = s_enabled ? MOTOR_STATE_ENABLED_IDLE : MOTOR_STATE_DISABLED;
    return ESP_OK;
}

bool motor_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    uint32_t report_hz = s_enabled ? s_step_hz : 0;
    int written = snprintf(buf, len,
                           "{\"state\":\"%s\",\"enabled\":%s,"
                           "\"step_hz\":%u,\"dir\":\"%s\","
                           "\"fault_code\":%d,\"fault_reason\":\"%s\"}",
                           motor_state_to_str(s_state),
                           s_enabled ? "true" : "false",
                           (unsigned)report_hz,
                           motor_dir_to_str(s_dir),
                           s_fault_code,
                           s_fault_reason);
    return (written >= 0 && (size_t)written < len);
}
