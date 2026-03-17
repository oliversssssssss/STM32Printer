/*
 * dotmatrix_converter_debug.h
 *
 * 点阵调试输出接口。
 * 本层职责：
 * - 把字符串转换成结构化点阵帧
 * - 不直接关心底层走哪个 UART
 */

#ifndef INC_DOTMATRIX_CONVERTER_DEBUG_H_
#define INC_DOTMATRIX_CONVERTER_DEBUG_H_

#include <stdint.h>
#include "print_settings.h"

void dm_print_string_debug(const char *text);
void dm_print_string_with_settings(const char *text, const PrintSettings *s);

#endif /* INC_DOTMATRIX_CONVERTER_DEBUG_H_ */
