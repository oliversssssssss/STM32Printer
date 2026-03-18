#ifndef MYPRINTF_H
#define MYPRINTF_H

#include <stdint.h>
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 兼容保留：旧接口名 */
void uart2_printf(const char *fmt, ...);

/* 运行时绑定 UART1 日志互斥锁
 * - 在内核启动前，不需要 mutex，日志仍可直接发送
 * - 在任务运行后，日志发送会优先走 mutex 保护
 */
void log_bind_uart1_mutex(osMutexId_t mutex_handle);

/* 通用日志输出 */
void log_printf(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* MYPRINTF_H */
