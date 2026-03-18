/*
 * print_settings.h
 *
 * 打印排版参数统一定义。
 * 说明：此前 PrintSettings 实际定义在 dotmatrix_converter_debug.h 中，
 * 这里将其收口到独立头文件，便于命令层与点阵层共享。
 */
#ifndef INC_PRINT_SETTINGS_H_
#define INC_PRINT_SETTINGS_H_

#include <stdint.h>

typedef struct {
    uint8_t line_spacing;   /* 行间距（像素） */
    uint8_t margin_left;    /* 左页边距（像素） */
    uint8_t margin_right;   /* 右页边距（像素） */
    uint8_t scale;          /* 放大倍数 */
    uint8_t alignment;      /* 0=左, 1=中, 2=右 */
} PrintSettings;

#endif /* INC_PRINT_SETTINGS_H_ */
