#ifndef INC_APP_RECEIVE_H_
#define INC_APP_RECEIVE_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Receive 模块当前保留两条能力：
 *
 * 1. 旧能力（legacy 主链）
 *    - uart_rx_start()
 *    - uart_GetDate()
 *    这条链继续给旧的 step 打印使用
 *
 * 2. 新增能力（为后续 stream/parser 铺路）
 *    - uart_rx_pop_chunk(...)
 *    只负责从 ring + frame queue 中取出“原始 chunk”
 *    不做命令/文本分流
 *
 * 当前阶段要求：
 * - 绝不破坏旧的 uart_GetDate() 行为
 * - 只是增加新接口，不替换旧主链
 */

void uart_rx_start(void);

/* 旧主链：取一帧并直接做 legacy 命令/文本分流 */
void uart_GetDate(void);

/*
 * 新接口：从接收环形缓冲 + 帧长度队列中取出一个原始 chunk
 *
 * 参数：
 *   buf      - 用户提供的输出缓冲
 *   buf_size - 输出缓冲大小
 *   out_len  - 实际取出的长度
 *
 * 返回：
 *   true  - 成功取出一段原始 chunk
 *   false - 当前没有可取数据，或参数非法
 *
 * 注意：
 * - 该接口只“取原始块”，不负责命令/文本判断
 * - 该接口当前不会被旧主链调用
 */
bool uart_rx_pop_chunk(uint8_t *buf, uint16_t buf_size, uint16_t *out_len);

#endif /* INC_APP_RECEIVE_H_ */
