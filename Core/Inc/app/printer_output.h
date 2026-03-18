/*
 * printer_output.h
 *
 * 打印输出抽象层。
 * 当前阶段：
 * - 实际通过 USART2 输出打印调试数据
 * - 提供结构化帧协议输出接口
 */

#ifndef INC_PRINTER_OUTPUT_H_
#define INC_PRINTER_OUTPUT_H_

#include <stdint.h>

/* 基础发送接口 */
void printer_output_send_text(const char *text);
void printer_output_send_buffer(const uint8_t *data, uint16_t len);

/* 结构化帧输出接口 */
void printer_output_frame_begin(const char *mode, uint16_t width, uint16_t height);
void printer_output_frame_row_bits(const char *row_bits);
void printer_output_frame_end(void);

#endif /* INC_PRINTER_OUTPUT_H_ */
