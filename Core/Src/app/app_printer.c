#include "app_printer.h"
#include "main.h"
#include "usart.h"
#include "Receive.h"
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "escpos_commands.h"
#include "print_buffer.h"

/* ========================= 内部子流程 ========================= */

/* 接收处理子流程
 * 当前职责：
 * 1. 消费 UART1 环形缓冲中的已接收字节
 * 2. 完成空闲超时判帧
 * 3. 将一帧分发为：
 *    - 文本写入 print_buffer
 *    - 命令交给 escpos_commands 处理
 *
 * 未来 FreeRTOS 视角：
 * - 这里可以很自然演进为“接收处理任务”的主体逻辑
 */
static void app_printer_process_rx(void)
{
    uart_GetDate();
}

/* 打印处理子流程
 * 当前职责：
 * 1. 检查是否存在待执行打印请求
 * 2. 若存在，则统一执行打印
 */
static void app_printer_process_print(void)
{
    printer_process_execute_request();
}

/* ========================= 对外接口 ========================= */

void app_printer_init(void)
{
    print_buffer_init();

    log_info("U575 port start\r\n");
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

    dm_print_string_debug("start");
}

void app_printer_process(void)
{

    app_printer_process_rx();
    app_printer_process_print();
}
