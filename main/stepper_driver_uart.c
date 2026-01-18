

// TMC2209 1-wire UART (PDN_UART) reference (KNOWN-GOOD)
// UART: UART1 @ 115200 8N1, TX=GPIO17, RX=GPIO18
// Wiring (critical):
//   - GPIO17 (TX) -> ~1k series resistor -> PDN_UART node
//   - GPIO18 (RX) -> direct -> PDN_UART node
//   - PDN/UART pin on module -> PDN_UART node
// Meter check (power off):
//   - GPIO18 to PDN pin ~= 0 ohms
//   - GPIO17 to PDN pin ~= 1k ohms
// Frames:
//   - Request (4B): 05 <slave_addr 0..3> <reg> <crc>
//   - Reply   (8B): 05 FF <reg> <data0..3> <crc>
// 1-wire behavior:
//   - RX typically receives 12B: 4B echo (request) + 8B reply.
// RX parsing rule:
//   - Accumulate RX up to ~50ms.
//   - If rx_total >= 12: use last 8 bytes as reply.
//   - If rx_total == 8: use those 8 bytes as reply.
// Validate:
//   - reply[0]==0x05, reply[1]==0xFF, reply[2]==reg, CRC over reply[0..6] matches reply[7].
// CRC:
//   - CRC8 poly 0x07, init 0, bits fed LSB-first, CRC shifts left each bit.
// Failure signatures:
//   - rx_total==0: RX not on PDN node (wiring/junction wrong).
//   - rx_total==4: echo only -> TMC not accepting frame (CRC/config/routing/power).


#include "stepper_driver_uart.h"

#include <string.h>
#include <stdio.h>

#include "board.h"
#include "esp_log.h"
#include "events.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STEPPER_UART UART_NUM_1
#define STEPPER_UART_BAUD 115200
#define STEPPER_UART_BUF 512
#define STEPPER_UART_TX_GPIO 17
#define STEPPER_UART_RX_GPIO 18
#define STEPPER_UART_DEBUG 0

static const char *TAG = "stepper_uart";

#define TMC_SYNC 0x05
#define TMC_SLAVE_ADDR 0x00

#define TMC_REG_GCONF 0x00
#define TMC_REG_GSTAT 0x01
#define TMC_REG_IFCNT 0x02
#define TMC_REG_IHOLD_IRUN 0x10
#define TMC_REG_CHOPCONF 0x6C
#define TMC_REG_DRV_STATUS 0x6F

#define TMC_GCONF_EN_SPREADCYCLE (1U << 2)
#define TMC_GSTAT_RESET_MASK 0x07

static bool s_uart_ready = false;
static uint16_t s_microsteps = 16;
static uint8_t s_run_current = 0;
static uint8_t s_hold_current = 0;
static uint8_t s_hold_delay = 0;
static bool s_stealthchop = true;

static void format_hex_bytes(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0)
    {
        return;
    }
    size_t used = 0;
    if (data == NULL)
    {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < len; ++i)
    {
        int written = snprintf(out + used, out_len - used, "%s%02X", (i == 0) ? "" : " ", data[i]);
        if (written < 0 || (size_t)written >= out_len - used)
        {
            break;
        }
        used += (size_t)written;
    }
}

static uint8_t tmc_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t cur = data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            if (((crc >> 7) ^ (cur & 0x01)) != 0)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            }
            else
            {
                crc <<= 1;
            }
            cur >>= 1;
        }
    }
    return crc;
}

static esp_err_t tmc_uart_write(const uint8_t *data, size_t len)
{
    if (!s_uart_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = uart_flush_input(STEPPER_UART);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_flush_input failed: %s", esp_err_to_name(err));
        return err;
    }
    int written = uart_write_bytes(STEPPER_UART, data, len);
    if (written != (int)len)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void tmc_uart_drain_rx(TickType_t max_ticks)
{
    uint8_t dump[16];
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < max_ticks)
    {
        int read = uart_read_bytes(STEPPER_UART, dump, sizeof(dump), 1);
        if (read <= 0)
        {
            break;
        }
    }
}

static bool tmc_find_reply(const uint8_t *rx, size_t total, uint8_t reg, const uint8_t **reply_out)
{
    if (rx == NULL || reply_out == NULL || total < 8)
    {
        return false;
    }
    const uint8_t reg_masked = (uint8_t)(reg & 0x7F);
    const uint8_t *reply = NULL;
    for (size_t i = 0; i + 8 <= total; ++i)
    {
        const uint8_t *cand = &rx[i];
        if (cand[0] != TMC_SYNC || cand[1] != 0xFF)
        {
            continue;
        }
        if ((cand[2] & 0x7F) != reg_masked)
        {
            continue;
        }
        uint8_t crc = tmc_crc(cand, 7);
        if (crc != cand[7])
        {
            continue;
        }
        reply = cand;
    }
    if (reply == NULL)
    {
        return false;
    }
    *reply_out = reply;
    return true;
}

static esp_err_t tmc_read_reg_addr(uint8_t addr, uint8_t reg, uint32_t *out, bool emit_events)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    (void)emit_events;
    uint8_t req[4] = {TMC_SYNC, addr, (uint8_t)(reg & 0x7F), 0};
    req[3] = tmc_crc(req, 3);
    esp_err_t err = tmc_uart_write(req, sizeof(req));
    if (err != ESP_OK)
    {
        return err;
    }
    uart_wait_tx_done(STEPPER_UART, pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(2));
    if (STEPPER_UART_DEBUG)
    {
        char tx_hex[32];
        format_hex_bytes(req, sizeof(req), tx_hex, sizeof(tx_hex));
        ESP_LOGI(TAG, "tx len=%u data=%s", (unsigned)sizeof(req), tx_hex);
    }

    // RX can be 8-byte reply-only, or 12-byte (4-byte echo + 8-byte reply).
    uint8_t rx[12] = {0};
    const TickType_t timeout_ticks = pdMS_TO_TICKS(10);
    const TickType_t read_timeout_ticks = pdMS_TO_TICKS(10);
    const TickType_t overall_timeout_ticks = pdMS_TO_TICKS(50);
    TickType_t start_ticks = xTaskGetTickCount();
    int read = uart_read_bytes(STEPPER_UART, rx, sizeof(rx), timeout_ticks);
    size_t rx_len = (read > 0) ? (size_t)read : 0;
    if (STEPPER_UART_DEBUG && rx_len > 0)
    {
        char rx_hex[48];
        format_hex_bytes(rx, rx_len, rx_hex, sizeof(rx_hex));
        ESP_LOGI(TAG, "rx len=%d timeout_ticks=%u data=%s", read, (unsigned)timeout_ticks, rx_hex);
    }
    size_t total = rx_len;
    while (total < sizeof(rx))
    {
        TickType_t now = xTaskGetTickCount();
        if ((now - start_ticks) >= overall_timeout_ticks)
        {
            break;
        }
        int more = uart_read_bytes(STEPPER_UART, rx + total, sizeof(rx) - total, read_timeout_ticks);
        if (more > 0)
        {
            total += (size_t)more;
        }
    }
    if (STEPPER_UART_DEBUG && total > 0)
    {
        char total_hex[48];
        format_hex_bytes(rx, total, total_hex, sizeof(total_hex));
        ESP_LOGI(TAG, "rx total=%u deadline_ms=50 data=%s", (unsigned)total, total_hex);
    }
    if (total < 8)
    {
        if (total > 0)
        {
            char rx_hex[48];
            size_t dump_len = (total > 16) ? 16 : total;
            format_hex_bytes(rx, dump_len, rx_hex, sizeof(rx_hex));
            ESP_LOGE(TAG, "reply_invalid rx_len=%u data=%s", (unsigned)total, rx_hex);
            return ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGE(TAG, "rx_len=%u", (unsigned)total);
        return ESP_ERR_TIMEOUT;
    }
    const uint8_t *resp = NULL;
    if (!tmc_find_reply(rx, total, reg, &resp))
    {
        char rx_hex[48];
        size_t dump_len = (total > 16) ? 16 : total;
        format_hex_bytes(rx, dump_len, rx_hex, sizeof(rx_hex));
        ESP_LOGE(TAG, "reply_invalid rx_len=%u data=%s", (unsigned)total, rx_hex);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "reply_ok reg=0x%02X data=%02X %02X %02X %02X",
             resp[2], resp[3], resp[4], resp[5], resp[6]);
    *out = ((uint32_t)resp[3] << 24) |
           ((uint32_t)resp[4] << 16) |
           ((uint32_t)resp[5] << 8) |
           ((uint32_t)resp[6]);
    return ESP_OK;
}

static esp_err_t tmc_read_reg(uint8_t reg, uint32_t *out)
{
    return tmc_read_reg_addr(TMC_SLAVE_ADDR, reg, out, true);
}

static void tmc_log_addr_scan(uint8_t reg)
{
    uint32_t val = 0;
    bool found = false;
    for (uint8_t addr = 0; addr < 4; ++addr)
    {
        if (tmc_read_reg_addr(addr, reg, &val, false) == ESP_OK)
        {
            events_emit("driver_uart", "motor", addr, "addr_ok");
            found = true;
        }
    }
    if (!found)
    {
        events_emit("driver_uart", "motor", 255, "addr_none");
    }
}

static esp_err_t tmc_write_reg(uint8_t reg, uint32_t value)
{
    uint8_t req[8] = {
        TMC_SYNC,
        TMC_SLAVE_ADDR,
        (uint8_t)(reg | 0x80),
        (uint8_t)((value >> 24) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)(value & 0xFF),
        0};
    req[7] = tmc_crc(req, 7);
    if (reg == TMC_REG_IHOLD_IRUN)
    {
        char tx_hex[32];
        format_hex_bytes(req, sizeof(req), tx_hex, sizeof(tx_hex));
        ESP_LOGI(TAG, "ihold_irun_write tx=%s crc=0x%02X", tx_hex, req[7]);
    }
    esp_err_t err = tmc_uart_write(req, sizeof(req));
    if (err != ESP_OK)
    {
        return err;
    }
    uart_wait_tx_done(STEPPER_UART, pdMS_TO_TICKS(20));
    err = uart_flush_input(STEPPER_UART);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_flush_input failed: %s", esp_err_to_name(err));
        return err;
    }
    tmc_uart_drain_rx(pdMS_TO_TICKS(5));
    return ESP_OK;
}

static bool mres_from_microsteps(uint16_t microsteps, uint8_t *out)
{
    switch (microsteps)
    {
    case 256: *out = 0; return true;
    case 128: *out = 1; return true;
    case 64: *out = 2; return true;
    case 32: *out = 3; return true;
    case 16: *out = 4; return true;
    case 8: *out = 5; return true;
    case 4: *out = 6; return true;
    case 2: *out = 7; return true;
    case 1: *out = 8; return true;
    default: return false;
    }
}

static uint16_t microsteps_from_mres(uint8_t mres)
{
    switch (mres)
    {
    case 0: return 256;
    case 1: return 128;
    case 2: return 64;
    case 3: return 32;
    case 4: return 16;
    case 5: return 8;
    case 6: return 4;
    case 7: return 2;
    case 8: return 1;
    default: return 0;
    }
}

esp_err_t stepper_driver_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = STEPPER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(STEPPER_UART, &cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    const int tx_pin = STEPPER_UART_TX_GPIO;
    const int rx_pin = STEPPER_UART_RX_GPIO;
    err = uart_set_pin(STEPPER_UART,
                       tx_pin,
                       rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_driver_install(STEPPER_UART, STEPPER_UART_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_set_rx_timeout(STEPPER_UART, 2);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_rx_timeout failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_flush_input(STEPPER_UART);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_flush_input failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "UART%d init baud=%d tx=%d rx=%d rxbuf=%d",
             (int)STEPPER_UART,
             STEPPER_UART_BAUD,
             tx_pin,
             rx_pin,
             STEPPER_UART_BUF);
    s_uart_ready = true;
    return ESP_OK;
}

esp_err_t stepper_driver_read_ifcnt(uint8_t *out)
{
    uint32_t val = 0;
    esp_err_t err = tmc_read_reg(TMC_REG_IFCNT, &val);
    if (err != ESP_OK)
    {
        return err;
    }
    if (out != NULL)
    {
        *out = (uint8_t)(val & 0xFF);
    }
    return ESP_OK;
}

esp_err_t stepper_driver_ping(void)
{
    uint8_t ifcnt = 0;
    if (stepper_driver_read_ifcnt(&ifcnt) == ESP_OK)
    {
        events_emit("driver_uart", "motor", 0, "ok");
        return ESP_OK;
    }
    if (stepper_driver_read_ifcnt(&ifcnt) == ESP_OK)
    {
        events_emit("driver_uart", "motor", 0, "ok");
        return ESP_OK;
    }
    tmc_log_addr_scan(TMC_REG_IFCNT);
    return ESP_ERR_TIMEOUT;
}

esp_err_t stepper_driver_set_stealthchop(bool enable)
{
    uint32_t gconf = 0;
    esp_err_t err = tmc_read_reg(TMC_REG_GCONF, &gconf);
    if (err != ESP_OK)
    {
        return err;
    }
    if (enable)
    {
        gconf &= ~TMC_GCONF_EN_SPREADCYCLE;
    }
    else
    {
        gconf |= TMC_GCONF_EN_SPREADCYCLE;
    }
    err = tmc_write_reg(TMC_REG_GCONF, gconf);
    if (err != ESP_OK)
    {
        return err;
    }
    s_stealthchop = enable;
    events_emit("driver_mode", "motor", 0, enable ? "stealthchop" : "spreadcycle");
    return ESP_OK;
}

esp_err_t stepper_driver_set_microsteps(uint16_t microsteps)
{
    uint8_t mres = 0;
    if (!mres_from_microsteps(microsteps, &mres))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t chopconf = 0;
    esp_err_t err = tmc_read_reg(TMC_REG_CHOPCONF, &chopconf);
    if (err != ESP_OK)
    {
        return err;
    }
    chopconf &= ~(0x0F << 24);
    chopconf |= ((uint32_t)mres << 24);
    err = tmc_write_reg(TMC_REG_CHOPCONF, chopconf);
    if (err != ESP_OK)
    {
        return err;
    }
    s_microsteps = microsteps;
    events_emit("driver_microsteps", "motor", 0, "set");
    return ESP_OK;
}

esp_err_t stepper_driver_set_current(uint8_t run, uint8_t hold, uint8_t hold_delay)
{
    if (run > 31 || hold > 31 || hold_delay > 15)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t val = ((uint32_t)hold & 0x1F) |
                   (((uint32_t)run & 0x1F) << 8) |
                   (((uint32_t)hold_delay & 0x0F) << 16);
    ESP_LOGI(TAG, "set_current run=%u hold=%u hold_delay=%u val=0x%08X reg=0x10",
             (unsigned)run, (unsigned)hold, (unsigned)hold_delay, (unsigned)val);
    esp_err_t err = tmc_write_reg(TMC_REG_IHOLD_IRUN, val);
    if (err != ESP_OK)
    {
        return err;
    }
    s_run_current = run;
    s_hold_current = hold;
    s_hold_delay = hold_delay;
    events_emit("driver_current", "motor", 0, "set");
    return ESP_OK;
}

esp_err_t stepper_driver_clear_faults(void)
{
    esp_err_t err = tmc_write_reg(TMC_REG_GSTAT, TMC_GSTAT_RESET_MASK);
    if (err != ESP_OK)
    {
        return err;
    }
    events_emit("driver_fault_clear", "motor", 0, "clear");
    return ESP_OK;
}

bool stepper_driver_get_status_json(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return false;
    }
    uint32_t ifcnt = 0;
    uint32_t gstat = 0;
    uint32_t drv_status = 0;
    uint32_t chopconf = 0;
    uint32_t gconf = 0;

    bool ok_ifcnt = (tmc_read_reg(TMC_REG_IFCNT, &ifcnt) == ESP_OK);
    bool ok_gstat = (tmc_read_reg(TMC_REG_GSTAT, &gstat) == ESP_OK);
    bool ok_drv = (tmc_read_reg(TMC_REG_DRV_STATUS, &drv_status) == ESP_OK);
    bool ok_chop = (tmc_read_reg(TMC_REG_CHOPCONF, &chopconf) == ESP_OK);
    bool ok_gconf = (tmc_read_reg(TMC_REG_GCONF, &gconf) == ESP_OK);

    char ifcnt_buf[8];
    char gstat_buf[12];
    char drv_buf[12];
    char micro_buf[8];
    char run_buf[8];
    char hold_buf[8];
    char hold_delay_buf[8];
    char stst_buf[8];
    char cs_buf[8];
    const char *ifcnt_str = "null";
    const char *gstat_str = "null";
    const char *drv_str = "null";
    const char *micro_str = "null";
    const char *run_str = "null";
    const char *hold_str = "null";
    const char *hold_delay_str = "null";
    const char *stst_str = "null";
    const char *cs_str = "null";
    const char *stealth_str = "null";

    if (ok_ifcnt)
    {
        snprintf(ifcnt_buf, sizeof(ifcnt_buf), "%u", (unsigned)(ifcnt & 0xFF));
        ifcnt_str = ifcnt_buf;
    }
    if (ok_gstat)
    {
        snprintf(gstat_buf, sizeof(gstat_buf), "0x%02X", (unsigned)(gstat & 0xFF));
        gstat_str = gstat_buf;
    }
    if (ok_drv)
    {
        snprintf(drv_buf, sizeof(drv_buf), "0x%08X", (unsigned)drv_status);
        drv_str = drv_buf;
    }
    if (ok_chop)
    {
        uint8_t mres = (uint8_t)((chopconf >> 24) & 0x0F);
        uint16_t micro = microsteps_from_mres(mres);
        if (micro != 0)
        {
            snprintf(micro_buf, sizeof(micro_buf), "%u", (unsigned)micro);
            micro_str = micro_buf;
        }
    }
    snprintf(run_buf, sizeof(run_buf), "%u", (unsigned)s_run_current);
    snprintf(hold_buf, sizeof(hold_buf), "%u", (unsigned)s_hold_current);
    snprintf(hold_delay_buf, sizeof(hold_delay_buf), "%u", (unsigned)s_hold_delay);
    run_str = run_buf;
    hold_str = hold_buf;
    hold_delay_str = hold_delay_buf;
    if (ok_drv)
    {
        uint8_t stst = (uint8_t)((drv_status >> 31) & 0x01);
        uint8_t cs_actual = (uint8_t)((drv_status >> 16) & 0x1F);
        snprintf(stst_buf, sizeof(stst_buf), "%u", (unsigned)stst);
        snprintf(cs_buf, sizeof(cs_buf), "%u", (unsigned)cs_actual);
        stst_str = stst_buf;
        cs_str = cs_buf;
    }
    if (ok_gconf)
    {
        bool stealth = ((gconf & TMC_GCONF_EN_SPREADCYCLE) == 0);
        stealth_str = stealth ? "true" : "false";
    }

    int written = snprintf(buf, len,
                           "{\"ifcnt\":%s,\"gstat\":%s,\"drv_status\":%s,"
                           "\"microsteps\":%s,\"run_current\":%s,\"hold_current\":%s,"
                           "\"hold_delay_cmd\":%s,\"stst\":%s,\"cs_actual\":%s,"
                           "\"stealthchop\":%s}",
                           ifcnt_str, gstat_str, drv_str,
                           micro_str, run_str, hold_str,
                           hold_delay_str, stst_str, cs_str,
                           stealth_str);
    return (written >= 0 && (size_t)written < len);
}
