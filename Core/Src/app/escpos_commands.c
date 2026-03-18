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
 * 说明：
 * - settings 仍然只由本文件拥有
 * - 外部仍然不能直接修改 settings
 * - 当前打印仍走 DEBUG 模式，方便 Python 端直观看字
 */

#include "escpos_commands.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "print_buffer.h"
#include "print_settings.h"

/* ========================= 状态归属说明 =========================
 *
 * 【事件共享状态 / Event-like shared state】
 * printer_execute_request
 * - 兼容保留的旧标志位
 * - 非 RTOS / 回退路径下仍可轮询消费
 *
 * g_print_task_handle
 * - RTOS 下 PrintTask 的句柄
 * - 命令层通过它向 PrintTask 发通知
 *
 * g_print_semaphore
 * - RTOS 下的打印请求计数信号量
 * - 每次释放对应一次打印请求
 *
 * 【配置共享状态 / Config-shared state】
 * settings
 * - 当前由命令层内部 helper 修改
 * - 当前打印主链维护它，但 DEBUG 模式下暂未直接消费其排版效果
 * - RTOS 下通过 g_settings_mutex 受保护
 * ============================================================ */

#define PRINTER_EXECUTE_FLAG   (1UL << 0)

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

    /* 内核未运行，或未绑定 mutex，则直接通过 */
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

/* 内部无锁默认值恢复，只能在已持锁上下文中调用 */
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

static void printer_reset_to_default(void)
{
    printer_clear_text_buffer("ESC @ reset");
    printer_execute_request = 0U;

    if (!settings_lock(100)) {
        return;
    }

    printer_settings_reset_default_unlocked();
    settings_unlock();
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
    /* RTOS 优先路径：如果已经绑定了打印信号量，则用计数信号量排队打印请求 */
    if (g_print_semaphore != NULL) {
        (void)osSemaphoreRelease(g_print_semaphore);
        return;
    }

    /* 兼容路径：若仍走任务标志，则继续支持 */
    if (g_print_task_handle != NULL) {
        (void)osThreadFlagsSet(g_print_task_handle, PRINTER_EXECUTE_FLAG);
        return;
    }

    /* 回退路径：旧裸标志 */
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

    /* 打印前复制一份 settings 快照。
     * 如果拿快照失败，则继续使用上面的默认配置，避免未初始化风险。
     */
    (void)printer_copy_settings_snapshot(&settings_snapshot);

    log_info("Start printing the buffer...\r\n");

    /* 恢复 SETTINGS 模式：
     * - alignment
     * - margin_left / margin_right
     * - line_spacing
     * - scale
     * 都将真正参与打印
     */
    dm_print_string_with_settings(g_print_snapshot, &settings_snapshot);
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
    /* 优先使用计数信号量：每次释放对应一次打印请求 */
    if (g_print_semaphore != NULL) {
        if (osSemaphoreAcquire(g_print_semaphore, osWaitForever) == osOK) {
            printer_execute_buffer();
        }
        return;
    }

    /* 兼容：若还在用任务标志 */
    if (g_print_task_handle != NULL) {
        uint32_t flags = osThreadFlagsWait(PRINTER_EXECUTE_FLAG, osFlagsWaitAny, osWaitForever);

        if (((flags & osFlagsError) == 0U) && ((flags & PRINTER_EXECUTE_FLAG) != 0U)) {
            printer_execute_buffer();
        }
        return;
    }

    /* 回退：旧轮询 */
    printer_process_execute_request();
    osDelay(5);
}

void handle_escpos_command(uint8_t *cmd, uint8_t len)
{
    if (cmd == NULL || len == 0U) {
        return;
    }

    log_command_frame(cmd, len);

    /* =========================================================
     * 第一类：demo 简化触发命令（当前阶段保留）
     * ========================================================= */

    /* 0A 00 -> 请求打印并额外输出一个 CRLF（demo 简化协议） */
    if (len >= 2U && cmd[0] == 0x0A && cmd[1] == 0x00) {
        printer_request_execute();
        log_printf("\r\n");
        return;
    }

    /* 0C 00 -> 请求打印（demo 简化协议） */
    if (len >= 2U && cmd[0] == 0x0C && cmd[1] == 0x00) {
        printer_request_execute();
        return;
    }

    /* =========================================================
     * 第二类：ESC 开头命令
     * ========================================================= */
    if (cmd[0] != 0x1B) {
        log_error("Unknown command frame: first byte is not ESC\r\n");
        return;
    }

    /* ESC @ -> 初始化打印机（清打印缓冲区，恢复默认模式） */
    if (len >= 2U && cmd[1] == 0x40) {
        printer_reset_to_default();
        log_info("ESC @ : printer reset to default\r\n");
        return;
    }

    /* ESC d n -> 向前走纸 n 行（当前 demo 用输出空行模拟） */
    if (len >= 3U && cmd[1] == 'd') {
        uint8_t n = cmd[2];
        log_info("ESC d %d (Feed %d lines)\r\n", n, n);

        for (uint8_t i = 0U; i < n; i++) {
            log_printf("\r\n");
        }
        return;
    }

    /* ESC a n -> 对齐（左 / 中 / 右） */
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

    /* ESC 3 n -> 行间距 */
    if (len >= 3U && cmd[1] == '3') {
        printer_settings_set_line_spacing(cmd[2]);
        log_info("The line spacing is set to %d\r\n", settings.line_spacing);
        return;
    }

    /* =========================================================
     * 第三类：当前 demo 自定义 ESC 命令
     * ========================================================= */

    /* ESC L n -> 当前 demo 自定义为左边距 */
    if (len >= 3U && cmd[1] == 0x4C) {
        printer_settings_set_margin_left(cmd[2]);
        log_info("The left margin is set to %d\r\n", settings.margin_left);
        return;
    }

    /* ESC r n -> 当前 demo 自定义为右边距 */
    if (len >= 3U && cmd[1] == 'r') {
        printer_settings_set_margin_right(cmd[2]);
        log_info("The right margin is set to %d\r\n", settings.margin_right);
        return;
    }

    /* ESC E n -> 当前 demo 自定义为放大倍数 */
    if (len >= 3U && cmd[1] == 0x45) {
        printer_settings_set_scale(cmd[2]);
        log_info("The magnification is set to %d\r\n", settings.scale);
        return;
    }

    /* ESC 1D -> 当前 demo 自定义“切纸模拟” */
    if (len >= 2U && cmd[1] == 0x1D) {
        log_info("Demo cut-paper simulation\r\n");
        dm_print_string_debug("________________________");
        return;
    }

    /* 未识别命令 */
    log_error("Unknown ESC command\r\n");
}
