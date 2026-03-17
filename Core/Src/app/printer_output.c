/*
 * printer_output.c
 *
 * 打印输出抽象层实现。
 * 当前阶段：
 * - 使用 huart2 发送打印调试输出
 * - 提供结构化帧协议发送
 */

#include "printer_output.h"
#include "usart.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include <stdio.h>

static void printer_output_uart2_send_raw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    HAL_UART_Transmit(&huart2, (uint8_t *)data, len, HAL_MAX_DELAY);
}

void printer_output_send_buffer(const uint8_t *data, uint16_t len)
{
    printer_output_uart2_send_raw(data, len);
}

void printer_output_send_text(const char *text)
{
    if (text == NULL) {
        return;
    }

    printer_output_send_buffer((const uint8_t *)text, (uint16_t)strlen(text));
}

void printer_output_frame_begin(const char *mode, uint16_t width, uint16_t height)
{
    char header[128];
    if (mode == NULL) {
        mode = "UNKNOWN";
    }

    snprintf(header, sizeof(header),
             "@FRAME_BEGIN\r\n"
             "TYPE=DOTMATRIX\r\n"
             "MODE=%s\r\n"
             "WIDTH=%u\r\n"
             "HEIGHT=%u\r\n",
             mode, width, height);

    printer_output_send_text(header);
}

void printer_output_frame_row_bits(const char *row_bits)
{
    if (row_bits == NULL) {
        return;
    }

    printer_output_send_text("ROW=");
    printer_output_send_text(row_bits);
    printer_output_send_text("\r\n");
}

void printer_output_frame_end(void)
{
    printer_output_send_text("@FRAME_END\r\n");
}
