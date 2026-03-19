#ifndef __RECEIVE_H__
#define __RECEIVE_H__

#include "main.h"
#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * Receive.h
 *
 * 当前接收链对外暴露的正式接口只有两个：
 *
 * 1. uart_rx_start()
 *    - 启动 UART1 ReceiveToIdle 接收链
 *    - 在系统初始化阶段调用一次
 *
 * 2. uart_GetDate()
 *    - 任务侧消费已完整接收的一帧
 *    - 当前由 RxProcessTask 周期调用
 *
 * 说明：
 * - 当前主接收链已不再依赖旧的“单字节 HAL_UART_Receive_IT(..., 1)”模型
 * - 若工程中仍存在旧中断回调符号引用，对应兼容空壳定义在 Receive.c 中
 * ========================================================= */

void uart_rx_start(void);
void uart_GetDate(void);

/* 兼容保留：
 * 旧工程中若还有别处引用 rx_byte，则保留对外声明。
 * 当前方案 B 下，它已不再作为主接收路径核心变量。
 */
extern uint8_t rx_byte;

#ifdef __cplusplus
}
#endif

#endif
