#include "app_printer.h"
#include "main.h"
#include "usart.h"
#include "Receive.h"
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "escpos_commands.h"
#include "print_buffer.h"

/* ========================= FreeRTOS 候选子流程 ========================= */

/* 接收处理子流程 */
void app_printer_process_rx(void)
{
    uart_GetDate();
}

/* 兼容保留：旧轮询式打印处理子流程 */
void app_printer_process_print(void)
{
    printer_process_execute_request();
}

/* RTOS 下的新打印任务入口：阻塞等待打印请求 */
void app_printer_wait_print_task(void)
{
    printer_wait_and_process_execute_request();
}

/* ========================= 对外接口 ========================= */

void app_printer_init(void)
{
    print_buffer_init();

    log_info("U575 port start\r\n");
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

    dm_print_string_debug("start");
}

/* 兼容保留：裸机/单任务版本的总入口 */
void app_printer_process(void)
{
    app_printer_process_rx();
    app_printer_process_print();
}
