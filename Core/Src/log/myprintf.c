#include "myprintf.h"
#include "usart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_BUFFER_SIZE 256

/* 0 = 运行期静音，只保留内核未启动前日志
 * 1 = 运行期也输出日志（当前阶段不要打开）
 */
#define LOG_RUNTIME_ENABLE 0

/* 当前阶段日志统一走 USART1。 */
static osMutexId_t g_uart1_log_mutex = NULL;

/* 绑定 UART1 日志发送互斥锁 */
void log_bind_uart1_mutex(osMutexId_t mutex_handle)
{
    g_uart1_log_mutex = mutex_handle;
}

/* 内部统一发送口
 * 规则：
 * 1. 内核未运行前：允许直接发送（用于启动早期日志）
 * 2. 内核运行后：当前阶段直接静音，避免 UART1 日志继续冲击接收链
 */
static void log_uart1_send(const char *str)
{
    if (str == NULL) {
        return;
    }

    /* 启动早期日志：允许输出 */
    if (osKernelGetState() != osKernelRunning) {
        HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
        return;
    }

#if LOG_RUNTIME_ENABLE
    if (g_uart1_log_mutex != NULL) {
        if (osMutexAcquire(g_uart1_log_mutex, 100) == osOK) {
            HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
            (void)osMutexRelease(g_uart1_log_mutex);
        }
    }
#else
    (void)g_uart1_log_mutex;
    return;
#endif
}

/* 旧接口：保留兼容 */
void uart2_printf(const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    log_uart1_send(buffer);
}

/* 普通输出 */
void log_printf(const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    log_uart1_send(buffer);
}

/* 信息日志 */
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

/* 错误日志 */
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

/* 调试日志 */
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
