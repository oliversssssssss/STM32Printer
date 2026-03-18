/*
 * print_buffer.h
 *
 * 打印缓冲区模块接口。
 * 目标：
 * - 隐藏底层全局数组与长度变量
 * - 统一通过接口访问
 * - 在 RTOS 下通过模块内部 mutex 保护共享缓冲
 */

#ifndef INC_PRINT_BUFFER_H_
#define INC_PRINT_BUFFER_H_

#include <stdint.h>
#include "cmsis_os2.h"

#define PRINT_BUFFER_SIZE 4096U

/* 运行时绑定 print_buffer 的 mutex
 * - 内核未运行前：可不绑定，接口仍可直接工作
 * - 内核运行后：若已绑定 mutex，则缓冲访问自动受保护
 */
void print_buffer_bind_mutex(osMutexId_t mutex_handle);

/* 初始化/清空 */
void print_buffer_init(void);
void print_buffer_clear(void);

/* 追加文本到缓冲区
 * 成功返回 1，失败返回 0
 */
uint8_t print_buffer_append_text(const uint8_t *data, uint16_t len);

/* 查询接口 */
const char *print_buffer_get_data(void);
uint16_t print_buffer_get_length(void);
uint8_t print_buffer_is_empty(void);

/* RTOS 下推荐接口：
 * 原子地把当前缓冲内容拷贝到 out，并清空共享缓冲
 * 返回拷贝长度，0 表示当前无内容或失败
 *
 * 设计目的：
 * - PrintTask 不直接长期占用共享缓冲
 * - 打印期间新到文本仍可进入新的空缓冲
 */
uint16_t print_buffer_take_snapshot_and_clear(char *out, uint16_t out_size);

#endif /* INC_PRINT_BUFFER_H_ */
