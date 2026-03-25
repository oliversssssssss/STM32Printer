/*
 * print_stream_parser.h
 *
 *  Created on: 2026年3月24日
 *      Author: Administrator
 */

#ifndef INC_PRINT_STREAM_PARSER_H_
#define INC_PRINT_STREAM_PARSER_H_

#include <stdint.h>

/*
 * Step3 目标：
 * 1. 新增流式 parser 模块本身
 * 2. 先不接入 UART 主链
 * 3. 先只支持最小可用命令子集，供后续接入时复用
 *
 * 当前阶段不修改 legacy 主打印链：
 * - app_printer_process_rx() 仍然走旧 uart_GetDate()
 * - 旧 step 小票必须继续正常打印
 */

typedef enum {
    PARSER_TEXT = 0,
    PARSER_ESC_WAIT_CMD,
    PARSER_ESC_WAIT_ARG
} PrintParserState;

/* 初始化全局 parser */
void print_stream_parser_init(void);

/* 重置 parser 整体状态 */
void print_stream_parser_reset(void);

/* 喂入单个字节 */
void print_stream_parser_feed_byte(uint8_t b);

/* 喂入一段连续字节 */
void print_stream_parser_feed_chunk(const uint8_t *buf, uint16_t len);

#endif /* INC_PRINT_STREAM_PARSER_H_ */
