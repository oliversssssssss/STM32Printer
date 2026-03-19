#include "app_printer.h"
#include "main.h"
#include "usart.h"
#include "Receive.h"
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "escpos_commands.h"
#include "print_buffer.h"

/* =========================================================
 * app_printer.c
 *
 * 当前角色：
 * - 应用桥接层
 * - 负责把“启动 / 接收处理 / 打印等待 / 兼容入口”整理成统一入口
 *
 * 当前 RTOS 主链：
 * 1. RxProcessTask
 *    -> app_printer_process_rx()
 *    -> uart_GetDate()
 *
 * 2. PrintTask
 *    -> app_printer_wait_print_task()
 *    -> printer_wait_and_process_execute_request()
 *
 * 说明：
 * - app_printer_process() 仍保留，但仅作为旧裸机/单任务兼容入口
 * - 当前系统主链已不再依赖 app_printer_process()
 * ========================================================= */

/* ========================= 接收处理入口 =========================
 * 当前由 RxProcessTask 周期调用。
 * 任务侧消费已接收完成的帧，并分流到：
 * - 文本链
 * - 命令链
 */
void app_printer_process_rx(void)
{
    uart_GetDate();
}

/* ========================= 旧打印处理兼容入口 =========================
 * 兼容保留：
 * - 旧轮询路径下由主循环调用
 * - 当前 RTOS 主链已不再依赖它作为 PrintTask 顶层入口
 */
void app_printer_process_print(void)
{
    printer_process_execute_request();
}

/* ========================= RTOS 打印任务入口 =========================
 * 当前由 PrintTask 调用。
 * 内部阻塞等待打印请求信号量 / 标志位，再执行一次正式打印。
 */
void app_printer_wait_print_task(void)
{
    printer_wait_and_process_execute_request();
}

/* ========================= 初始化入口 =========================
 * 当前 main() 中在 RTOS 启动前调用。
 *
 * 作用：
 * 1. 初始化 print_buffer
 * 2. 打启动日志
 * 3. 启动 UART1 接收链（方案 B：ReceiveToIdle）
 * 4. 保留一次 DEBUG 输出，用于确认点阵与 UART2 输出链工作
 */
void app_printer_init(void)
{
    print_buffer_init();

    log_info("U575 port start\r\n");

    /* 方案 B：
     * 当前 UART1 主接收链采用 ReceiveToIdle。
     * 已不再使用旧的 HAL_UART_Receive_IT(&huart1, &rx_byte, 1)
     * 作为正式主接收方式。
     */
    uart_rx_start();

    /* 启动阶段保留一次 DEBUG 输出，便于确认：
     * - 点阵转换链是否工作
     * - UART2 结构化帧输出是否工作
     */
    dm_print_string_debug("start");
}

/* ========================= 兼容保留总入口 =========================
 * 旧裸机 / 单任务版本的总入口。
 *
 * 当前 RTOS 双任务主链已不再依赖该函数。
 * 保留它只是为了：
 * - 兼容历史调用方式
 * - 方便对比旧架构
 */
void app_printer_process(void)
{
    app_printer_process_rx();
    app_printer_process_print();
}
