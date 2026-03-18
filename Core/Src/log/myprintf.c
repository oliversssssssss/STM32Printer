#include "myprintf.h"
#include "usart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_BUFFER_SIZE 256

/* 当前阶段日志统一走 USART1。后续做双 UART 时，日志仍建议保留在 UART1。 */
static osMutexId_t g_uart1_log_mutex = NULL;

/* 绑定 UART1 日志发送互斥锁 */
void log_bind_uart1_mutex(osMutexId_t mutex_handle)
{
    g_uart1_log_mutex = mutex_handle;
}

/* 内部统一发送口
 * 规则：
 * 1. 内核未运行前：直接发送（用于启动早期日志）
 * 2. 内核运行后：若已绑定 mutex，则先拿锁再发送
 * 3. 若拿锁失败，则直接丢弃本次日志，避免任务间抢串口
 *
 * 说明：
 * - 当前我们不在 ISR 里打日志
 * - 后续也应继续避免在 ISR 中调用这些日志接口
 */
static void log_uart1_send(const char *str)
{
    if (str == NULL) {
        return;
    }

    if ((g_uart1_log_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        if (osMutexAcquire(g_uart1_log_mutex, 100) == osOK) {
            HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
            (void)osMutexRelease(g_uart1_log_mutex);
        }
        return;
    }

    /* 内核未启动前，允许直接发送启动日志 */
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
}

/* 旧接口：保留兼容，内部仍然走 huart1 */
void uart2_printf(const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    log_uart1_send(buffer);
}

/* 新接口：普通输出 */
void log_printf(const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    log_uart1_send(buffer);
}

/* 新接口：信息日志 */
void log_info(const char *fmt, ...)
{
    char msg[220];
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    snprintf(buffer, sizeof(buffer), "[INFO] %s", msg);
    log_uart1_send(buffer);
}

/* 新接口：错误日志 */
void log_error(const char *fmt, ...)
{
    char msg[220];
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    snprintf(buffer, sizeof(buffer), "[ERROR] %s", msg);
    log_uart1_send(buffer);
}

/* 新接口：调试日志 */
void log_debug(const char *fmt, ...)
{
    char msg[220];
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    snprintf(buffer, sizeof(buffer), "[DEBUG] %s", msg);
    log_uart1_send(buffer);
}
