#ifndef APP_PRINTER_H
#define APP_PRINTER_H

#ifdef __cplusplus
extern "C" {
#endif

void app_printer_init(void);

/* 兼容保留：裸机/单任务下的总入口 */
void app_printer_process(void);

/* FreeRTOS 下的接收处理子流程 */
void app_printer_process_rx(void);

/* 兼容保留：旧轮询式打印处理 */
void app_printer_process_print(void);

/* 新增：RTOS 下阻塞等待打印请求的打印任务入口 */
void app_printer_wait_print_task(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PRINTER_H */
