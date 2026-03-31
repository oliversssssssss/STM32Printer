/*
 * escpos_commands.c
 *
 * ESC/POS 风格命令处理（当前阶段为“标准/近标准 + demo 自定义”混合实现）
 * 当前这一版的目的：
 * 1. 保持第六批 Step1 的缓冲边界收口成果
 * 2. 保持第六批 Step2 的 execute_request 收口成果
 * 3. 保持第六批 Step3 的 settings 收口成果
 * 4. 在 RTOS 下把“打印请求”从轮询标志升级成真正的任务通知
 * 5. 给 settings 增加 RTOS mutex 保护，并补配置快照接口
 *
 * 当前这一轮的额外目标：
 * 6. 空打印请求收口，不再连续空转
 * 7. 继续保持命令层近乎静音
 */

#include "escpos_commands.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "print_buffer.h"
#include "print_settings.h"

#define PRINTER_EXECUTE_FLAG         (1UL << 0)

/* 当前阶段保持安静 */
#define ESCPOS_VERBOSE_CMD_LOG       0

/* 兼容保留：旧打印请求标志 */
static volatile uint8_t printer_execute_request = 0U;

/* RTOS 下的打印任务句柄 */
static osThreadId_t g_print_task_handle = NULL;

/* RTOS 下的打印请求计数信号量 */
static osSemaphoreId_t g_print_semaphore = NULL;

/* RTOS 下的 settings mutex */
static osMutexId_t g_settings_mutex = NULL;

/* Config-shared：当前打印配置快照 */
static PrintSettings settings = {
    .line_spacing = 0,
    .margin_left  = 0,
    .margin_right = 0,
    .scale        = 1,
    .alignment    = 0
};

/* PrintTask 专用快照缓冲 */
static char g_print_snapshot[PRINT_BUFFER_SIZE];

/* ========================= 内部辅助函数 ========================= */

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
#if ESCPOS_VERBOSE_CMD_LOG
    if (reason != NULL) {
        log_debug("print buffer cleared: %s\r\n", reason);
    }
#else
    (void)reason;
#endif
}

/* 内部无锁默认值恢复，只能在已持锁上下文中调用 */
static void printer_settings_reset_default_unlocked(void)
{
    settings.line_spacing = 0U;
    settings.margin_left  = 0U;
    settings.margin_right = 0U;
    settings.scale        = 1U;
    settings.alignment    = 0U;
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

static void printer_drain_all_pending_requests(void)
{
    printer_execute_request = 0U;

    if ((g_print_semaphore != NULL) && (osKernelGetState() == osKernelRunning)) {
        while (osSemaphoreAcquire(g_print_semaphore, 0U) == osOK) {
            /* drain all stale pending requests */
        }
    }
}

static void printer_reset_to_default(void)
{
    printer_clear_text_buffer("ESC @ reset");
    printer_drain_all_pending_requests();

    if (!settings_lock(100)) {
        return;
    }

    printer_settings_reset_default_unlocked();
    settings_unlock();
}

static void log_command_frame(const uint8_t *cmd, uint8_t len)
{
#if ESCPOS_VERBOSE_CMD_LOG
    uint8_t b0 = (len > 0U) ? cmd[0] : 0U;
    uint8_t b1 = (len > 1U) ? cmd[1] : 0U;
    uint8_t b2 = (len > 2U) ? cmd[2] : 0U;

    log_debug("ESC CMD: 0x%02X 0x%02X 0x%02X...\r\n", b0, b1, b2);
#else
    (void)cmd;
    (void)len;
#endif
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

/* ========================= 对外接口 ========================= */

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
            /* 当前阶段不再刷 UART1 错误日志，避免继续扰动 RX */
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
        /* 说明当前被“空请求”唤醒，直接把积压的空请求一起清掉 */
        printer_drain_all_pending_requests();
        return;
    }

    (void)printer_copy_settings_snapshot(&settings_snapshot);
    (void)settings_snapshot;

    dm_print_string_debug(g_print_snapshot);
}

/* 兼容保留：旧轮询路径 */
void printer_process_execute_request(void)
{
    if (printer_consume_execute_request()) {
        printer_execute_buffer();
    }
}

/* RTOS 下的新路径：阻塞等待打印请求 */
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

    /* 0A 00 -> 请求打印 */
    if (len >= 2U && cmd[0] == 0x0A && cmd[1] == 0x00) {
        printer_request_execute();
        return;
    }

    /* 0C 00 -> 请求打印 */
    if (len >= 2U && cmd[0] == 0x0C && cmd[1] == 0x00) {
        printer_request_execute();
        return;
    }

    if (cmd[0] != 0x1B) {
        return;
    }

    /* ESC @ -> 初始化打印机（清打印缓冲区，恢复默认模式） */
    if (len >= 2U && cmd[1] == 0x40) {
        printer_reset_to_default();
        return;
    }

    /* ESC d n -> 当前阶段忽略模拟空行输出 */
    if (len >= 3U && cmd[1] == 'd') {
        return;
    }

    /* ESC a n -> 对齐 */
    if (len >= 3U && cmd[1] == 'a') {
        uint8_t val = 0U;

        if (!parse_alignment_value(cmd[2], &val)) {
            return;
        }

        printer_settings_set_alignment(val);
        return;
    }

    /* ESC 3 n -> 行间距 */
    if (len >= 3U && cmd[1] == '3') {
        printer_settings_set_line_spacing(cmd[2]);
        return;
    }

    /* ESC L n -> 左边距 */
    if (len >= 3U && cmd[1] == 0x4C) {
        printer_settings_set_margin_left(cmd[2]);
        return;
    }

    /* ESC r n -> 右边距 */
    if (len >= 3U && cmd[1] == 'r') {
        printer_settings_set_margin_right(cmd[2]);
        return;
    }

    /* ESC E n -> 放大倍数 */
    if (len >= 3U && cmd[1] == 0x45) {
        uint8_t scale = cmd[2];
        if (scale < 1U) {
            scale = 1U;
        }
        if (scale > 3U) {
            scale = 3U;
        }
        printer_settings_set_scale(scale);
        return;
    }

    /* ESC 1D -> 切纸模拟 */
    if (len >= 2U && cmd[1] == 0x1D) {
        dm_print_string_debug("________________________");
        return;
    }
}
