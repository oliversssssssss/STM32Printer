#include "Receive.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "escpos_commands.h"
#include "print_buffer.h"

/* =========================================================
 * Receive.c
 *
 * 当前接收链采用方案 B：UART1 ReceiveToIdle
 *
 * 主链说明：
 * 1. uart_rx_start()
 *    -> 调用 HAL_UARTEx_ReceiveToIdle_IT(...)
 *    -> 启动一段式接收
 *
 * 2. HAL_UARTEx_RxEventCallback(...)
 *    -> 串口一段数据接收完成并进入空闲时触发
 *    -> ISR 仅负责：
 *       - 把这一段数据写入 rx_ring
 *       - 把“这一段长度”压入帧长度队列
 *       - 重新启动下一轮 ReceiveToIdle
 *
 * 3. uart_GetDate()
 *    -> 当前由 RxProcessTask 周期调用
 *    -> 从帧长度队列中精确取出一帧
 *    -> 再分流到：
 *       - 文本帧 -> handle_text_data(...)
 *       - 命令帧 -> handle_escpos_command(...)
 *
 * 当前已不再采用旧的：
 * - 单字节 HAL_UART_Receive_IT(..., 1)
 * - 50ms 空闲超时猜一帧
 * 作为正式主接收路径。
 *
 * 兼容说明：
 * - HAL_UART_RxCpltCallback(...) 仍保留空壳，以避免旧引用报错
 * - rx_byte 仍保留定义，以避免头文件依赖断裂
 *
 * 当前这一轮的额外目标：
 * - 遇到 UART ORE/NE/FE/PE 后，显式清错误标志并 flush RX 数据，
 *   再重启 ReceiveToIdle，避免错误状态残留导致持续丢字节。
 * ========================================================= */

/* ========================= 参数配置 ========================= */

#define BUFFER_SIZE              4096
#define RX_IDLE_CHUNK_SIZE       256
#define RX_FRAME_QUEUE_SIZE      64
#define UART_GETDATE_MAX_FRAMES  8U

/* ========================= 接收状态 ========================= */

/* 环形缓冲：ISR 写，任务读 */
static uint8_t rx_ring[BUFFER_SIZE];

/* ISR / 任务共享索引 */
static volatile uint16_t readIndex  = 0U;
static volatile uint16_t writeIndex = 0U;

/* 兼容保留：旧单字节接收临时变量
 * 方案 B 下不再是主路径核心变量，仅保留定义避免旧引用出错。
 */
uint8_t rx_byte = 0U;

/* 最近一次接收完成的时间戳（当前主要作为调试观测使用） */
static volatile uint32_t rx_tick = 0U;

/* ReceiveToIdle 一段接收缓冲 */
static uint8_t rx_idle_chunk[RX_IDLE_CHUNK_SIZE];

/* 帧长度队列
 * ISR 把每一段接收的 Size 入队
 * 任务侧按队列长度精确取帧
 */
static volatile uint16_t rx_frame_len_queue[RX_FRAME_QUEUE_SIZE];
static volatile uint8_t  rx_frame_q_head  = 0U;
static volatile uint8_t  rx_frame_q_tail  = 0U;
static volatile uint8_t  rx_frame_q_count = 0U;

/* ========================= 可观测统计 ========================= */

/* 环形缓冲溢出：这一段放不下 */
static volatile uint32_t rx_ring_overflow_count   = 0U;
static volatile uint8_t  rx_ring_overflow_pending = 0U;

/* 帧长度队列溢出：队列满了，新的帧长度无法入队 */
static volatile uint32_t rx_frame_queue_overflow_count   = 0U;
static volatile uint8_t  rx_frame_queue_overflow_pending = 0U;

/* UART 错误统计 */
static volatile uint32_t uart_error_count     = 0U;
static volatile uint32_t uart_error_code_last = 0U;
static volatile uint8_t  uart_error_pending   = 0U;

/* ========================= 内部辅助函数 ========================= */

static uint16_t ring_used_length(void)
{
    return (uint16_t)((writeIndex + BUFFER_SIZE - readIndex) % BUFFER_SIZE);
}

static uint16_t ring_free_length(void)
{
    /* 预留 1 字节区分“满”和“空” */
    return (uint16_t)((BUFFER_SIZE - 1U) - ring_used_length());
}

/* 当前规则：
 * 1. ESC 开头 -> 命令帧
 * 2. demo 简化触发命令：仅严格接受 0A 00 / 0C 00
 *
 * 注意：
 * 当前仍然是“整帧二选一”模型：
 * - 整帧要么按命令处理
 * - 整帧要么按文本处理
 *
 * 这意味着：
 * 目前还不是“标准 ESC/POS 混合流 parser”。
 */
static uint8_t is_command_frame(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len == 0U) {
        return 0U;
    }

    if (frame[0] == 0x1B) {
        return 1U;
    }

    if (len == 2U && frame[1] == 0x00U) {
        if (frame[0] == 0x0AU || frame[0] == 0x0CU) {
            return 1U;
        }
    }

    return 0U;
}

static void handle_text_data(uint8_t *data, uint16_t len)
{
    while (len > 0U && (data[len - 1U] == '\r' || data[len - 1U] == '\n')) {
        len--;
    }

    if (len == 0U) {
        return;
    }

    if (!print_buffer_append_text(data, len)) {
        log_error("print_buffer append failed\r\n");
    }
}

/* 显式清 UART 错误并 flush RX 数据。
 * 目的：
 * - 处理 ORE / NE / FE / PE 后，不让错误状态残留
 * - 丢掉当前已经损坏/半截的 RX 数据
 * - 然后再重启 ReceiveToIdle
 */
static void uart_rx_clear_error_and_flush(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return;
    }

    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) != RESET) {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF);
    }

    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_NE) != RESET) {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_NEF);
    }

    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_FE) != RESET) {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_FEF);
    }

    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_PE) != RESET) {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_PEF);
    }

    /* 丢弃当前 FIFO / RDR 中可能残留的脏数据 */
    __HAL_UART_SEND_REQ(huart, UART_RXDATA_FLUSH_REQUEST);
}

/* 重新启动下一轮 ReceiveToIdle */
static void uart_rx_restart_idle(void)
{
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_idle_chunk, RX_IDLE_CHUNK_SIZE) != HAL_OK) {
        uart_error_count++;
        uart_error_pending = 1U;
        uart_error_code_last = 0xFFFFFFFFU;
    }
}

/* ========================= 对外接口 ========================= */

void uart_rx_start(void)
{
    readIndex = 0U;
    writeIndex = 0U;
    rx_tick = 0U;

    rx_frame_q_head  = 0U;
    rx_frame_q_tail  = 0U;
    rx_frame_q_count = 0U;

    rx_ring_overflow_count = 0U;
    rx_ring_overflow_pending = 0U;

    rx_frame_queue_overflow_count = 0U;
    rx_frame_queue_overflow_pending = 0U;

    uart_error_count = 0U;
    uart_error_code_last = 0U;
    uart_error_pending = 0U;

    memset(rx_ring, 0, sizeof(rx_ring));
    memset(rx_idle_chunk, 0, sizeof(rx_idle_chunk));

    uart_rx_clear_error_and_flush(&huart1);
    uart_rx_restart_idle();
}

/* ========================= HAL 回调 ========================= */

/* 方案 B 主回调：
 * 一段数据接收完成，并且 UART 进入空闲时触发。
 *
 * ISR 只做最小工作：
 * 1. 记录时间
 * 2. 检查队列容量
 * 3. 检查 ring 空间
 * 4. 整段写入 rx_ring
 * 5. 把帧长度 Size 入队
 * 6. 重启下一轮 ReceiveToIdle
 *
 * 不在 ISR 中做：
 * - 文本处理
 * - 命令解析
 * - 打印触发
 * - 复杂日志
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        rx_tick = HAL_GetTick();

        if (Size > 0U) {
            if (rx_frame_q_count >= RX_FRAME_QUEUE_SIZE) {
                rx_frame_queue_overflow_count++;
                rx_frame_queue_overflow_pending = 1U;
            }
            else if (Size > ring_free_length()) {
                rx_ring_overflow_count++;
                rx_ring_overflow_pending = 1U;
            }
            else {
                for (uint16_t i = 0; i < Size; i++) {
                    rx_ring[writeIndex] = rx_idle_chunk[i];
                    writeIndex = (uint16_t)((writeIndex + 1U) % BUFFER_SIZE);
                }

                rx_frame_len_queue[rx_frame_q_tail] = Size;
                rx_frame_q_tail = (uint8_t)((rx_frame_q_tail + 1U) % RX_FRAME_QUEUE_SIZE);
                rx_frame_q_count++;
            }
        }

        uart_rx_restart_idle();
    }
}

/* 兼容保留：
 * 当前方案 B 已不再使用单字节接收完成回调作为主路径。
 * 保留此空实现仅为了避免旧工程符号依赖报错。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
}

/* UART 错误回调：
 * - 记录错误
 * - 显式清错误标志并 flush RX 数据
 * - 尝试恢复接收链
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uart_error_count++;
        uart_error_code_last = huart->ErrorCode;
        uart_error_pending = 1U;

        uart_rx_clear_error_and_flush(huart);
        uart_rx_restart_idle();
    }
}

/* ========================= 任务侧处理 ========================= */

void uart_GetDate(void)
{
    static char frame[BUFFER_SIZE + 1U];
    uint8_t processed_frames = 0U;

    /* 任务侧统一输出可观测错误 */
    if (uart_error_pending != 0U) {
        uart_error_pending = 0U;
        log_error("UART1 error count=%lu, last=0x%08lX\r\n",
                  (unsigned long)uart_error_count,
                  (unsigned long)uart_error_code_last);
    }

    if (rx_ring_overflow_pending != 0U) {
        rx_ring_overflow_pending = 0U;
        log_error("UART1 rx_ring overflow, dropped frames=%lu\r\n",
                  (unsigned long)rx_ring_overflow_count);
    }

    if (rx_frame_queue_overflow_pending != 0U) {
        rx_frame_queue_overflow_pending = 0U;
        log_error("UART1 frame queue overflow, dropped frames=%lu\r\n",
                  (unsigned long)rx_frame_queue_overflow_count);
    }

    while (processed_frames < UART_GETDATE_MAX_FRAMES) {
        uint16_t frame_len = 0U;
        uint32_t primask = __get_PRIMASK();

        __disable_irq();

        if (rx_frame_q_count > 0U) {
            frame_len = rx_frame_len_queue[rx_frame_q_head];
            rx_frame_q_head = (uint8_t)((rx_frame_q_head + 1U) % RX_FRAME_QUEUE_SIZE);
            rx_frame_q_count--;
        }

        if (primask == 0U) {
            __enable_irq();
        }

        if (frame_len == 0U) {
            break;
        }

        /* 按 frame_len 精确从 ring 中取出这一帧 */
        uint16_t copy_len =
            (uint16_t)(((uint32_t)readIndex + frame_len <= BUFFER_SIZE)
                       ? frame_len
                       : (BUFFER_SIZE - readIndex));

        memcpy(frame, &rx_ring[readIndex], copy_len);

        if (copy_len < frame_len) {
            memcpy(frame + copy_len, rx_ring, frame_len - copy_len);
        }

        readIndex = (uint16_t)((readIndex + frame_len) % BUFFER_SIZE);
        frame[frame_len] = '\0';

        /* 当前仍按“整帧命令 / 整帧文本”分流 */
        if (is_command_frame((const uint8_t *)frame, frame_len)) {
            handle_escpos_command((uint8_t *)frame, frame_len);
        } else {
            handle_text_data((uint8_t *)frame, frame_len);
        }

        processed_frames++;
    }
}
