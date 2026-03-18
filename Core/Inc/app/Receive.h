/*
 * Receive.h
 *
 * 串口接收与超时组帧接口。
 */
#ifndef INC_RECEIVE_H_
#define INC_RECEIVE_H_

#include <stdint.h>

/* USART1 单字节接收缓冲，由 HAL_UART_Receive_IT 使用 */
extern uint8_t rx_byte;

/* 轮询解析入口：在主循环中周期性调用 */
void uart_GetDate(void);

#endif /* INC_RECEIVE_H_ */
