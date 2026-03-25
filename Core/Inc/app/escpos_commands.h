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
 *
 * 第二步目标：
 * - 在不破坏 legacy 打印主链的前提下，
 *   提取可复用的 settings helper，供后续新 parser 使用。
 */

#ifndef ESCPOS_COMMANDS_H
#define ESCPOS_COMMANDS_H

#include <stdint.h>
#include "cmsis_os2.h"
#include "print_settings.h"

/* 解析一帧命令数据（legacy 主入口，保持原行为） */
void handle_escpos_command(uint8_t *cmd, uint8_t len);

/* 兼容保留：轮询式处理 */
void printer_process_execute_request(void);

/* RTOS 下绑定打印任务句柄 */
void printer_bind_print_task(osThreadId_t task_handle);

/* RTOS 下绑定打印请求计数信号量 */
void printer_bind_print_semaphore(osSemaphoreId_t sem_handle);

/* RTOS 下阻塞等待打印请求并执行 */
void printer_wait_and_process_execute_request(void);

/* RTOS 下绑定 settings mutex */
void printer_bind_settings_mutex(osMutexId_t mutex_handle);

/* 获取当前打印设置快照
 * 成功返回 1，失败返回 0
 */
uint8_t printer_copy_settings_snapshot(PrintSettings *out);

/* 打印请求接口 */
void printer_request_execute(void);
uint8_t printer_consume_execute_request(void);

/* 兼容保留：直接返回 settings 指针
 * 注意：RTOS 下不建议跨任务长期持有该指针
 */
const PrintSettings *printer_get_settings(void);

/* 真正执行打印缓冲区（legacy 旧主链） */
void printer_execute_buffer(void);

/* =========================================================
 * Step2 新增：可复用 settings helper
 *
 * 说明：
 * - 当前 legacy 主链仍通过 handle_escpos_command() 工作
 * - 后续新 parser 不应该直接依赖 handle_escpos_command()
 * - 而应识别命令后，调用这些 helper 修改一个 settings struct
 * ========================================================= */

/* 将一个 settings 结构恢复到默认值 */
void printer_settings_reset_struct(PrintSettings *s);

/* 对一个 settings 结构应用单项设置 */
void printer_settings_apply_alignment(PrintSettings *s, uint8_t n);
void printer_settings_apply_line_spacing(PrintSettings *s, uint8_t n);
void printer_settings_apply_margin_left(PrintSettings *s, uint8_t n);
void printer_settings_apply_margin_right(PrintSettings *s, uint8_t n);
void printer_settings_apply_scale(PrintSettings *s, uint8_t n);

#endif /* ESCPOS_COMMANDS_H */
