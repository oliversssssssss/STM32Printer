#include "Receive.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "escpos_commands.h"
#include "print_buffer.h"

// 接收环形缓冲大小
#define BUFFER_SIZE 4096

// 串口空闲超过该时间，认为一帧结束
#define FRAME_IDLE_TIMEOUT_MS 50U

/* ========================= 状态归属说明 =========================
 *
 * 【ISR 共享状态组】
 * 下面这几个对象由“USART1 中断回调”和“主循环中的接收处理逻辑”共同参与：
 *
 * 1. rx_ring
 *    - ISR 写入：HAL_UART_RxCpltCallback()
 *    - 主循环读取：uart_GetDate()
 *    - 角色：字节流环形缓冲
 *
 * 2. writeIndex
 *    - ISR 写入
 *    - 主循环读取
 *    - 角色：环形缓冲写指针
 *
 * 3. readIndex
 *    - 主循环写入/读取
 *    - ISR 不直接改它
 *    - 但它与 writeIndex / rx_ring 一起构成 ISR-任务共享边界
 *
 * 4. rx_tick
 *    - ISR 更新时间戳
 *    - 主循环读取，用于空闲超时判帧
 *
 * 5. rx_byte
 *    - 当前单字节接收暂存变量
 *    - 中断接收链核心参与对象
 *
 * 未来 FreeRTOS 视角：
 * - 这一组状态应视为“ISR shared”
 * - 未来若迁移 RTOS，应优先考虑：
 *   1) 保持 ISR 只负责写入
 *   2) 任务侧统一消费
 *   3) 再进一步才考虑 stream buffer / queue / 临界区保护
 * ============================================================ */

/* ISR shared：主循环消费读指针 */
static uint16_t readIndex = 0;

/* ISR shared：中断写入写指针，主循环读取它判断缓冲长度 */
static uint16_t writeIndex = 0;

/* ISR shared：字节流环形缓冲，中断写、主循环读 */
static uint8_t rx_ring[BUFFER_SIZE];

/* ISR shared：单字节接收临时变量 */
uint8_t rx_byte;

/* ISR shared：最近一次接收到字节的时刻，中断更新，主循环读取 */
static uint32_t rx_tick;

// 获取环形缓冲里未读数据长度
static uint16_t GetBufferLength(void)
{
    return (writeIndex + BUFFER_SIZE - readIndex) % BUFFER_SIZE;
}

/* 判断当前一帧是否应按“命令帧”处理
 * 当前规则：
 * 1. ESC 开头 -> 命令帧
 * 2. 0x0A 开头 -> demo 简化触发命令帧
 * 3. 0x0C 开头 -> demo 简化触发命令帧
 */
static uint8_t is_command_frame(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len == 0U) {
        return 0U;
    }

    if (frame[0] == 0x1B) {
        return 1U;
    }

    if (frame[0] == 0x0A) {
        return 1U;
    }

    if (frame[0] == 0x0C) {
        return 1U;
    }

    return 0U;
}

/* 文本数据处理
 *
 * 【任务共享边界说明】
 * 文本帧一旦进入这里，就不再属于 ISR 共享状态组，
 * 而是转入 print_buffer 模块管理。
 *
 * 也就是说：
 * - Receive.c 只负责“把文本送进缓冲”
 * - 不拥有 print_buffer 的底层存储
 * - 不直接改 print_buf_len
 */
static void handle_text_data(uint8_t *data, uint16_t len)
{
    while (len > 0U && (data[len - 1U] == '\r' || data[len - 1U] == '\n')) {
        len--;
    }

    if (len == 0U) {
        return;
    }

    log_info("识别到指令txt\r\n");

    if (print_buffer_append_text(data, len)) {
        log_info("缓冲区已存入 %d 字节，总长 %d\r\n", len, print_buffer_get_length());
    } else {
        log_error("缓冲区溢出，丢弃数据\r\n");
    }

    // 如需直接观察文本，可打开：
    // dm_print_string_debug((char *)data);
}

// UART 中断回调：单字节接收写入环形缓冲
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uint16_t next = (uint16_t)((writeIndex + 1U) % BUFFER_SIZE);

        if (next != readIndex) {
            rx_ring[writeIndex] = rx_byte;
            writeIndex = next;
            rx_tick = HAL_GetTick();
        }

        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

/* 数据解析函数：超时组帧 -> 文本 / 命令分发
 *
 * 当前角色：
 * - 它属于“接收处理子流程”的主体
 * - 从 ISR shared 状态组中取出一帧
 * - 再把帧分发到：
 *   1) 文本缓冲（任务共享）
 *   2) 命令处理（配置/事件共享）
 *
 * 未来 FreeRTOS 视角：
 * - 这里是最自然的“接收处理任务”主体逻辑入口
 */
void uart_GetDate(void)
{
    uint32_t now = HAL_GetTick();
    uint16_t len = GetBufferLength();

    // 串口空闲一段时间后，判定一帧完成
    if (len > 0U && ((now - rx_tick) > FRAME_IDLE_TIMEOUT_MS)) {
        static char frame[BUFFER_SIZE];

        uint16_t copy_len = (readIndex + len <= BUFFER_SIZE) ? len : (BUFFER_SIZE - readIndex);
        memcpy(frame, &rx_ring[readIndex], copy_len);

        if (copy_len < len) {
            memcpy(frame + copy_len, rx_ring, len - copy_len);
        }

        frame[len] = '\0';

        // 命令帧
        if (is_command_frame((const uint8_t *)frame, len)) {
            handle_escpos_command((uint8_t *)frame, len);
        }
        // 普通文本帧
        else {
            handle_text_data((uint8_t *)frame, len);
        }

        // 更新读指针
        readIndex = (uint16_t)((readIndex + len) % BUFFER_SIZE);
    }
}
