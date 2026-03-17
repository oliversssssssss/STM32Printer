#ifndef MYPRINTF_H
#define MYPRINTF_H

#include <stdint.h>

/*
 * 旧接口：名字沿用历史版本，但当前实际仍走 USART1。
 * 后续若做双 UART，可根据需要决定是否保留。
 */
void uart2_printf(const char *fmt, ...);

/* 新统一日志接口 */
void log_printf(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#endif /* MYPRINTF_H */
