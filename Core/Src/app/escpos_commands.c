/*
 * escpos_commands.c
 *
 * 当前这一版只做一个关键修复：
 * - 正式打印从 DEBUG 渲染切回 SETTINGS 渲染
 * - 让 GUI 发送的对齐 / 放大 / 边距 / 行距真正生效
 *
 * 其余接收 / 打印请求 / RTOS 同步逻辑保持原样，避免破坏当前能完整打印的基线。
 */

#include "escpos_commands.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "print_buffer.h"
#include "print_settings.h"

#define PRINTER_EXECUTE_FLAG   (1UL << 0)

static volatile uint8_t printer_execute_request = 0U;
static osThreadId_t g_print_task_handle = NULL;
static osSemaphoreId_t g_print_semaphore = NULL;
static osMutexId_t g_settings_mutex = NULL;

static PrintSettings settings = {
    .line_spacing = 0,
    .margin_left  = 0,
    .margin_right = 0,
    .scale        = 1,
    .alignment    = 0
};

static char g_print_snapshot[PRINT_BUFFER_SIZE];

static uint8_t settings_lock(uint32_t timeout)
{
    if ((g_settings_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        return (osMutexAcquire(g_settings_mutex, timeout) == osOK) ? 1U : 0U;
    }
    return 1U;
}

static void settings_unlock(void)
{
    if ((g_settings_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        (void)osMutexRelease(g_settings_mutex);
    }
}

static void printer_clear_text_buffer(const char *reason)
{
    print_buffer_clear();
    if (reason != NULL) {
        log_debug("print buffer cleared: %s\r\n", reason);
    }
}

static void printer_settings_reset_default_unlocked(void)
{
    settings.line_spacing = 0U;
    settings.margin_left  = 0U;
    settings.margin_right = 0U;
    settings.scale        = 1U;
    settings.alignment    = 0U;
}

static void printer_settings_reset_default(void)
{
    if (!settings_lock(100)) {
        return;
    }
    printer_settings_reset_default_unlocked();
    settings_unlock();
}

static void printer_settings_set_alignment(uint8_t alignment)
{
    if (!settings_lock(100)) {
        return;
    }
    settings.alignment = alignment;
    settings_unlock();
}

static void printer_settings_set_line_spacing(uint8_t line_spacing)
{
    if (!settings_lock(100)) {
        return;
    }
    settings.line_spacing = line_spacing;
    settings_unlock();
}

static void printer_settings_set_margin_left(uint8_t margin_left)
{
    if (!settings_lock(100)) {
        return;
    }
    settings.margin_left = margin_left;
    settings_unlock();
}

static void printer_settings_set_margin_right(uint8_t margin_right)
{
    if (!settings_lock(100)) {
        return;
    }
    settings.margin_right = margin_right;
    settings_unlock();
}

static void printer_settings_set_scale(uint8_t scale)
{
    if (!settings_lock(100)) {
        return;
    }
    if (scale < 1U) {
        scale = 1U;
    }
    if (scale > 3U) {
        scale = 3U;
    }
    settings.scale = scale;
    settings_unlock();
}

static void printer_clear_pending_execute_requests(void)
{
    printer_execute_request = 0U;
    if ((g_print_semaphore != NULL) && (osKernelGetState() == osKernelRunning)) {
        while (osSemaphoreAcquire(g_print_semaphore, 0U) == osOK) {
        }
    }
}

static void printer_reset_to_default(void)
{
    printer_clear_text_buffer("ESC @ reset");
    printer_clear_pending_execute_requests();
    printer_settings_reset_default();
}

static void log_command_frame(const uint8_t *cmd, uint8_t len)
{
    uint8_t b0 = (len > 0U) ? cmd[0] : 0U;
    uint8_t b1 = (len > 1U) ? cmd[1] : 0U;
    uint8_t b2 = (len > 2U) ? cmd[2] : 0U;
    log_debug("ESC CMD: 0x%02X 0x%02X 0x%02X...\r\n", b0, b1, b2);
}

static uint8_t parse_alignment_value(uint8_t raw, uint8_t *out_val)
{
    if (out_val == NULL) {
        return 0U;
    }
    if (raw >= '0' && raw <= '2') {
        *out_val = (uint8_t)(raw - '0');
        return 1U;
    }
    if (raw <= 2U) {
        *out_val = raw;
        return 1U;
    }
    return 0U;
}

void printer_bind_print_task(osThreadId_t task_handle)
{
    g_print_task_handle = task_handle;
}

void printer_bind_print_semaphore(osSemaphoreId_t sem_handle)
{
    g_print_semaphore = sem_handle;
}

void printer_bind_settings_mutex(osMutexId_t mutex_handle)
{
    g_settings_mutex = mutex_handle;
}

uint8_t printer_copy_settings_snapshot(PrintSettings *out)
{
    if (out == NULL) {
        return 0U;
    }
    if (!settings_lock(100)) {
        return 0U;
    }
    *out = settings;
    settings_unlock();
    return 1U;
}

void printer_request_execute(void)
{
    if (g_print_semaphore != NULL) {
        osStatus_t status = osSemaphoreRelease(g_print_semaphore);
        if (status != osOK) {
            log_error("print request dropped: semaphore queue full\r\n");
        }
        return;
    }
    if (g_print_task_handle != NULL) {
        (void)osThreadFlagsSet(g_print_task_handle, PRINTER_EXECUTE_FLAG);
        return;
    }
    printer_execute_request = 1U;
}

uint8_t printer_consume_execute_request(void)
{
    if (printer_execute_request != 0U) {
        printer_execute_request = 0U;
        return 1U;
    }
    return 0U;
}

const PrintSettings *printer_get_settings(void)
{
    return &settings;
}

void printer_execute_buffer(void)
{
    PrintSettings settings_snapshot = {
        .line_spacing = 0U,
        .margin_left  = 0U,
        .margin_right = 0U,
        .scale        = 1U,
        .alignment    = 0U
    };

    uint16_t copied_len = print_buffer_take_snapshot_and_clear(g_print_snapshot, sizeof(g_print_snapshot));

    if (copied_len == 0U) {
        log_error("print_buffer is empty\r\n");
        return;
    }

    (void)printer_copy_settings_snapshot(&settings_snapshot);

    log_info("Start printing in SETTINGS mode...\r\n");
    dm_print_string_with_settings(g_print_snapshot, &settings_snapshot);
}

void printer_process_execute_request(void)
{
    if (printer_consume_execute_request()) {
        printer_execute_buffer();
    }
}

void printer_wait_and_process_execute_request(void)
{
    if (g_print_semaphore != NULL) {
        if (osSemaphoreAcquire(g_print_semaphore, osWaitForever) == osOK) {
            printer_execute_buffer();
        }
        return;
    }

    if (g_print_task_handle != NULL) {
        uint32_t flags = osThreadFlagsWait(PRINTER_EXECUTE_FLAG, osFlagsWaitAny, osWaitForever);
        if (((flags & osFlagsError) == 0U) && ((flags & PRINTER_EXECUTE_FLAG) != 0U)) {
            printer_execute_buffer();
        }
        return;
    }

    printer_process_execute_request();
    osDelay(5);
}

void handle_escpos_command(uint8_t *cmd, uint8_t len)
{
    if (cmd == NULL || len == 0U) {
        return;
    }

    log_command_frame(cmd, len);

    if (len >= 2U && cmd[0] == 0x0A && cmd[1] == 0x00) {
        printer_request_execute();
        log_printf("\r\n");
        return;
    }

    if (len >= 2U && cmd[0] == 0x0C && cmd[1] == 0x00) {
        printer_request_execute();
        return;
    }

    if (cmd[0] != 0x1B) {
        log_error("Unknown command frame: first byte is not ESC\r\n");
        return;
    }

    if (len >= 2U && cmd[1] == 0x40) {
        printer_reset_to_default();
        log_info("ESC @ : printer reset to default\r\n");
        return;
    }

    if (len >= 3U && cmd[1] == 'd') {
        uint8_t n = cmd[2];
        log_info("ESC d %d (Feed %d lines)\r\n", n, n);
        for (uint8_t i = 0U; i < n; i++) {
            log_printf("\r\n");
        }
        return;
    }

    if (len >= 3U && cmd[1] == 'a') {
        uint8_t val = 0U;
        if (!parse_alignment_value(cmd[2], &val)) {
            log_error("ESC a invalid n: %d\r\n", cmd[2]);
            return;
        }
        printer_settings_set_alignment(val);
        if (val == 0U) {
            log_info("Alignment is set to left (0)\r\n");
        } else if (val == 1U) {
            log_info("Alignment is set to center (1)\r\n");
        } else {
            log_info("Alignment is set to right (2)\r\n");
        }
        return;
    }

    if (len >= 3U && cmd[1] == '3') {
        printer_settings_set_line_spacing(cmd[2]);
        log_info("The line spacing is set to %d\r\n", cmd[2]);
        return;
    }

    if (len >= 3U && cmd[1] == 0x4C) {
        printer_settings_set_margin_left(cmd[2]);
        log_info("The left margin is set to %d\r\n", cmd[2]);
        return;
    }

    if (len >= 3U && cmd[1] == 'r') {
        printer_settings_set_margin_right(cmd[2]);
        log_info("The right margin is set to %d\r\n", cmd[2]);
        return;
    }

    if (len >= 3U && cmd[1] == 0x45) {
        uint8_t scale = cmd[2];
        if (scale < 1U) {
            scale = 1U;
        }
        if (scale > 3U) {
            scale = 3U;
        }
        printer_settings_set_scale(scale);
        log_info("The magnification is set to %d\r\n", scale);
        return;
    }

    if (len >= 2U && cmd[1] == 0x1D) {
        log_info("Demo cut-paper simulation\r\n");
        dm_print_string_debug("________________________");
        return;
    }

    log_error("Unknown ESC command\r\n");
}
