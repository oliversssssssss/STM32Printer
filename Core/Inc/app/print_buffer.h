/*
 * print_buffer.h
 *
 * 打印缓冲区模块接口。
 * 目标：
 * - 隐藏底层全局数组与长度变量
 * - 统一通过接口访问
 */

#ifndef INC_PRINT_BUFFER_H_
#define INC_PRINT_BUFFER_H_

#include <stdint.h>

#define PRINT_BUFFER_SIZE 4096U

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

#endif /* INC_PRINT_BUFFER_H_ */
