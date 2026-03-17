#include "myprintf.h"
#include "usart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_BUFFER_SIZE 256

/* 当前阶段日志统一走 USART1。后续做双 UART 时，日志仍建议保留在 UART1。 */
static void log_uart1_send(const char *str)
{
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
