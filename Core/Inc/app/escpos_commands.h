/*
 * escpos_commands.h
 *
 * ESC/POS 风格命令处理接口。
 * 说明：
 * 1. 当前命令层既包含标准/近标准命令，也保留部分 demo 自定义命令。
 * 2. 命令层负责：
 *    - 解释命令
 *    - 修改打印设置
 *    - 发起打印请求
 * 3. 真正执行打印由应用调度层统一触发。
 */

#ifndef ESCPOS_COMMANDS_H
#define ESCPOS_COMMANDS_H

#include <stdint.h>
#include "print_settings.h"

/* 解析一帧命令数据 */
void handle_escpos_command(uint8_t *cmd, uint8_t len);


void printer_process_execute_request(void);


/* 打印请求接口 */
void printer_request_execute(void);
uint8_t printer_consume_execute_request(void);

/* 当前打印设置（只读获取） */
const PrintSettings *printer_get_settings(void);

/* 真正执行打印缓冲区 */
void printer_execute_buffer(void);

#endif /* ESCPOS_COMMANDS_H */
