/*
 * dotmatrix_converter_debug.c
 *
 * 当前职责：
 * 1. 从 ku.bin 字库中读取 ASCII 16x16 点阵
 * 2. 按 settings 做基础排版（对齐、边距、行距、放大）
 * 3. 生成结构化 UART2 帧：
 *      @FRAME_BEGIN
 *      TYPE=DOTMATRIX
 *      MODE=DEBUG / SETTINGS
 *      WIDTH=...
 *      HEIGHT=...
 *      ROW=...
 *      ...
 *      @FRAME_END
 *
 * 注意：
 * - 本文件不直接调用 huart1 / huart2
 * - 真正的串口发送由 printer_output.c 负责
 */

#include "dotmatrix_converter_debug.h"
#include "ku_font.h"
#include "printer_output.h"
#include <string.h>

/* ASCII 范围设置（仅支持 0x20-0x7E） */
#define FIRST_CHAR_CODE     0x20
#define LAST_CHAR_CODE      0x7E
#define CHAR_WIDTH_PIXELS   16
#define CHAR_HEIGHT_PIXELS  16

/* 保留 1 列字符间隔，便于 Python 渲染时更清楚 */
#define CHAR_SPACING_PIXELS 1
#define FRAME_CHAR_WIDTH    (CHAR_WIDTH_PIXELS + CHAR_SPACING_PIXELS)

#define BYTES_PER_CHAR      (CHAR_WIDTH_PIXELS * CHAR_HEIGHT_PIXELS / 8)  /* 32 bytes */

#define MAX_LINES           50
#define CHARS_PER_LINE      24
#define MAX_FRAME_WIDTH     (CHARS_PER_LINE * FRAME_CHAR_WIDTH)           /* 24 * 17 = 408 */

/* 外部字库起始地址 */
extern const uint8_t _binary_ku_bin_start[];

/* 将任意字符限制到当前支持的 ASCII 范围；超出时映射为 '?' */
static char normalize_printable_char(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < FIRST_CHAR_CODE || uc > LAST_CHAR_CODE) {
        return '?';
    }
    return c;
}

/* DEBUG 模式下，为了让英文单词更容易看清，
 * 临时把小写字母转成大写显示。
 * 注意：
 * - 只用于 DEBUG 显示
 * - 不改变缓冲区原始文本
 * - 不影响后续 SETTINGS 模式
 */
static char normalize_debug_display_char(char c)
{
    char normalized = normalize_printable_char(c);

    if (normalized >= 'a' && normalized <= 'z') {
        return (char)(normalized - 'a' + 'A');
    }

    return normalized;
}

/* 根据字符获得字库中对应字符的起始地址 */
static const uint8_t *get_font_char_ptr(char c)
{
    char normalized = normalize_printable_char(c);
    uint16_t offset = (uint16_t)(normalized - FIRST_CHAR_CODE) * BYTES_PER_CHAR;
    return &_binary_ku_bin_start[offset];
}

/* 读取原始 16x16 位图到 out_bits[row] 中（bit15..bit0） */
static void load_font_bits_16x16(const uint8_t *font_char, uint16_t out_bits[CHAR_HEIGHT_PIXELS])
{
    for (int r = 0; r < CHAR_HEIGHT_PIXELS; r++) {
        uint8_t b1 = font_char[r * 2];
        uint8_t b2 = font_char[r * 2 + 1];
        out_bits[r] = ((uint16_t)b1 << 8) | (uint16_t)b2;
    }
}

/* 在 16x16 空间内做简单 n×n 膨胀 */
static void dilate_font_bits_16x16(const uint16_t in_bits[CHAR_HEIGHT_PIXELS],
                                   uint16_t out_bits[CHAR_HEIGHT_PIXELS],
                                   int scale)
{
    for (int r = 0; r < CHAR_HEIGHT_PIXELS; r++) {
        out_bits[r] = 0;
    }

    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;

    int off = scale / 2;

    for (int y = 0; y < CHAR_HEIGHT_PIXELS; y++) {
        for (int x = 0; x < CHAR_WIDTH_PIXELS; x++) {
            if (in_bits[y] & (1u << (15 - x))) {
                for (int dy = -off; dy <= scale - 1 - off; dy++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= CHAR_HEIGHT_PIXELS) continue;

                    for (int dx = -off; dx <= scale - 1 - off; dx++) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= CHAR_WIDTH_PIXELS) continue;

                        out_bits[yy] |= (1u << (15 - xx));
                    }
                }
            }
        }
    }
}

/* 初始化一整行为 '0' */
static void fill_zero_row(char *row_buf, int width)
{
    for (int i = 0; i < width; i++) {
        row_buf[i] = '0';
    }
    row_buf[width] = '\0';
}

/* 将 16 位字符一行点阵写入到 row_buf 的指定起始位置 */
static void write_char_bits_to_row(char *row_buf, int row_width, int start_x, uint16_t bits)
{
    for (int b = 0; b < CHAR_WIDTH_PIXELS; b++) {
        int dst_x = start_x + b;
        if (dst_x < 0 || dst_x >= row_width) {
            continue;
        }

        row_buf[dst_x] = (bits & (1u << (15 - b))) ? '1' : '0';
    }
}

/* ---------------- DEBUG 模式 ----------------
 * 特点：
 * - 不考虑 margins / alignment / scale / line_spacing
 * - 直接把字符逐个排开
 * - WIDTH = 当前帧最长行字符数 * 17
 * - HEIGHT = 行数 * 16
 */
void dm_print_string_debug(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    char processed_lines[MAX_LINES][CHARS_PER_LINE + 1];
    memset(processed_lines, 0, sizeof(processed_lines));

    int line_count = 0;
    int max_chars_in_line = 0;

    /* 按 \n 拆行，每行最多 CHARS_PER_LINE 个字符 */
    const char *start = text;
    while (line_count < MAX_LINES && *start) {
        int line_len = (int)strcspn(start, "\n");
        if (line_len > CHARS_PER_LINE) {
            line_len = CHARS_PER_LINE;
        }

        strncpy(processed_lines[line_count], start, (size_t)line_len);
        processed_lines[line_count][line_len] = '\0';

        if (line_len > max_chars_in_line) {
            max_chars_in_line = line_len;
        }

        start += line_len;
        if (*start == '\n') {
            start++;
        }

        line_count++;
    }

    if (line_count == 0) {
        return;
    }

    int frame_width = max_chars_in_line * FRAME_CHAR_WIDTH;
    if (frame_width <= 0) {
        frame_width = FRAME_CHAR_WIDTH;
    }

    int frame_height = line_count * CHAR_HEIGHT_PIXELS;

    printer_output_frame_begin("DEBUG", (uint16_t)frame_width, (uint16_t)frame_height);

    for (int line_idx = 0; line_idx < line_count; line_idx++) {
        size_t len = strlen(processed_lines[line_idx]);

        for (int row = 0; row < CHAR_HEIGHT_PIXELS; row++) {
            char row_buf[MAX_FRAME_WIDTH + 1];
            fill_zero_row(row_buf, frame_width);

            for (size_t i = 0; i < len && i < CHARS_PER_LINE; i++) {
                char debug_char = normalize_debug_display_char(processed_lines[line_idx][i]);
                const uint8_t *font_char = get_font_char_ptr(debug_char);

                uint16_t bits[CHAR_HEIGHT_PIXELS];
                load_font_bits_16x16(font_char, bits);

                int start_x = (int)i * FRAME_CHAR_WIDTH;
                write_char_bits_to_row(row_buf, frame_width, start_x, bits[row]);
            }

            printer_output_frame_row_bits(row_buf);
        }
    }

    printer_output_frame_end();
}

/* ---------------- SETTINGS 模式 ----------------
 * 特点：
 * - 应用 settings 中的：
 *      alignment
 *      margin_left
 *      margin_right
 *      line_spacing
 *      scale
 * - WIDTH 固定为 MAX_FRAME_WIDTH = 408
 * - HEIGHT = line_count * (16 + line_spacing)
 */
void dm_print_string_with_settings(const char *text, const PrintSettings *s)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    char processed_lines[MAX_LINES][CHARS_PER_LINE + 1];
    memset(processed_lines, 0, sizeof(processed_lines));

    int line_count = 0;
    int scale = 1;
    int left_margin = 0;
    int right_margin = 0;
    int alignment = 0;
    int line_spacing = 0;

    if (s) {
        scale = s->scale;
        if (scale < 1) scale = 1;
        if (scale > 3) scale = 3;

        left_margin = s->margin_left;
        right_margin = s->margin_right;
        alignment = s->alignment;
        line_spacing = s->line_spacing;
    }

    const int frame_width = MAX_FRAME_WIDTH;
    const int usable_pixels_per_line = frame_width - left_margin - right_margin;
    const int can_place_any_char = (usable_pixels_per_line >= FRAME_CHAR_WIDTH) ? 1 : 0;

    /* 按像素宽度拆行 */
    const char *p = text;
    int cur_pos = 0;
    int used_pixels = left_margin;

    while (*p && line_count < MAX_LINES) {
        if (*p == '\n') {
            processed_lines[line_count][cur_pos] = '\0';
            line_count++;
            cur_pos = 0;
            used_pixels = left_margin;
            p++;
            continue;
        }

        char c = normalize_printable_char(*p);

        if (used_pixels + FRAME_CHAR_WIDTH > frame_width - right_margin) {
            processed_lines[line_count][cur_pos] = '\0';
            line_count++;
            if (line_count >= MAX_LINES) break;

            cur_pos = 0;
            used_pixels = left_margin;

            if (!can_place_any_char ||
                used_pixels + FRAME_CHAR_WIDTH > frame_width - right_margin) {
                p++;
                continue;
            }
        }

        if (cur_pos >= CHARS_PER_LINE) {
            processed_lines[line_count][cur_pos] = '\0';
            line_count++;
            if (line_count >= MAX_LINES) break;

            cur_pos = 0;
            used_pixels = left_margin;

            if (!can_place_any_char ||
                used_pixels + FRAME_CHAR_WIDTH > frame_width - right_margin) {
                p++;
                continue;
            }
        }

        processed_lines[line_count][cur_pos++] = c;
        used_pixels += FRAME_CHAR_WIDTH;
        p++;
    }

    if (line_count < MAX_LINES) {
        processed_lines[line_count][cur_pos] = '\0';
        line_count++;
    }

    if (line_count == 0) {
        return;
    }

    int frame_height = line_count * (CHAR_HEIGHT_PIXELS + line_spacing);
    printer_output_frame_begin("SETTINGS", (uint16_t)frame_width, (uint16_t)frame_height);

    for (int line_idx = 0; line_idx < line_count; line_idx++) {
        size_t len = strlen(processed_lines[line_idx]);

        /* 预计算本行每个字符膨胀后的 16 行点阵 */
        uint16_t dilated[CHARS_PER_LINE][CHAR_HEIGHT_PIXELS];
        memset(dilated, 0, sizeof(dilated));

        for (size_t i = 0; i < len && i < CHARS_PER_LINE; i++) {
            const uint8_t *font_char = get_font_char_ptr(processed_lines[line_idx][i]);

            uint16_t in_bits[CHAR_HEIGHT_PIXELS];
            uint16_t out_bits[CHAR_HEIGHT_PIXELS];
            memset(in_bits, 0, sizeof(in_bits));
            memset(out_bits, 0, sizeof(out_bits));

            load_font_bits_16x16(font_char, in_bits);
            dilate_font_bits_16x16(in_bits, out_bits, scale);

            for (int r = 0; r < CHAR_HEIGHT_PIXELS; r++) {
                dilated[i][r] = out_bits[r];
            }
        }

        int content_pixels = (int)len * FRAME_CHAR_WIDTH;
        int avail_pixels = frame_width - left_margin - right_margin;
        if (avail_pixels < 0) {
            avail_pixels = 0;
        }

        int extra_left = 0;
        if (content_pixels <= avail_pixels) {
            if (alignment == 1) {
                extra_left = (avail_pixels - content_pixels) / 2;
            } else if (alignment == 2) {
                extra_left = (avail_pixels - content_pixels);
            }
        }

        int start_x_base = left_margin + extra_left;

        /* 输出 16 行点阵 */
        for (int row = 0; row < CHAR_HEIGHT_PIXELS; row++) {
            char row_buf[MAX_FRAME_WIDTH + 1];
            fill_zero_row(row_buf, frame_width);

            for (size_t i = 0; i < len && i < CHARS_PER_LINE; i++) {
                int start_x = start_x_base + (int)i * FRAME_CHAR_WIDTH;
                write_char_bits_to_row(row_buf, frame_width, start_x, dilated[i][row]);
            }

            printer_output_frame_row_bits(row_buf);
        }

        /* 行间距：输出若干全 0 行 */
        for (int sp = 0; sp < line_spacing; sp++) {
            char blank_row[MAX_FRAME_WIDTH + 1];
            fill_zero_row(blank_row, frame_width);
            printer_output_frame_row_bits(blank_row);
        }
    }

    printer_output_frame_end();
}
