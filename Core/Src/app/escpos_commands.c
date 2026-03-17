/*
 * escpos_commands.c
 *
 * ESC/POS 风格命令处理（当前阶段为“标准/近标准 + demo 自定义”混合实现）
 * 当前这一版的目的：
 * 1. 保持第六批 Step1 的缓冲边界收口成果
 * 2. 保持第六批 Step2 的 execute_request 收口成果
 * 3. 保持第六批 Step3 的 settings 收口成果
 * 4. 推进第七批 Step3：把“事件共享 / 配置共享”身份明确标注进代码
 *
 * 说明：
 * - settings 仍然只由本文件拥有
 * - 外部仍然不能直接修改 settings
 * - 当前打印仍走 DEBUG 模式，方便 Python 端直观看字
 */

#include "escpos_commands.h"
#include "stm32u5xx_hal.h"
#include <string.h>
#include "myprintf.h"
#include "dotmatrix_converter_debug.h"
#include "print_buffer.h"
#include "print_settings.h"

/* ========================= 状态归属说明 =========================
 *
 * 【事件共享状态 / Event-like shared state】
 * printer_execute_request
 * - 当前由命令层置位
 * - 当前由命令层统一消费
 * - 本质上代表“有一次待执行打印事件”
 *
 * 未来 FreeRTOS 视角：
 * - 它比“普通共享变量”更像 event flag / task notify / queue item
 * - 后续迁移 RTOS 时，优先考虑事件化，而不是长期保留裸标志位
 *
 * 【配置共享状态 / Config-shared state】
 * settings
 * - 当前由命令层内部 helper 修改
 * - 当前打印主链维护它，但 DEBUG 模式下暂未直接消费其排版效果
 * - 未来恢复 SETTINGS 模式时，打印执行链会读取它
 *
 * 未来 FreeRTOS 视角：
 * - 它适合作为“配置快照对象”
 * - 更适合低频写 / 读取当前快照，而不是高频竞争修改
 * ============================================================ */

/* Event-shared：打印请求标志
 * - 命令处理置位
 * - 命令层统一消费
 */
static volatile uint8_t printer_execute_request = 0;

/* Config-shared：当前打印配置快照
 * - 当前只允许本文件内部 helper 修改
 * - 外部只允许只读获取
 */
static PrintSettings settings = {
    .line_spacing = 0,
    .margin_left  = 0,
    .margin_right = 0,
    .scale        = 1,
    .alignment    = 0
};

/* ========================= 内部辅助函数 ========================= */

/* 统一缓冲区清空入口 */
static void printer_clear_text_buffer(const char *reason)
{
    print_buffer_clear();

    if (reason != NULL) {
        log_debug("print buffer cleared: %s\r\n", reason);
    }
}

/* Config-shared：settings 默认值恢复 */
static void printer_settings_reset_default(void)
{
    settings.line_spacing = 0U;
    settings.margin_left  = 0U;
    settings.margin_right = 0U;
    settings.scale        = 1U;
    settings.alignment    = 0U;
}

/* Config-shared：settings 内部 setter - 对齐 */
static void printer_settings_set_alignment(uint8_t alignment)
{
    settings.alignment = alignment;
}

/* Config-shared：settings 内部 setter - 行间距 */
static void printer_settings_set_line_spacing(uint8_t line_spacing)
{
    settings.line_spacing = line_spacing;
}

/* Config-shared：settings 内部 setter - 左边距 */
static void printer_settings_set_margin_left(uint8_t margin_left)
{
    settings.margin_left = margin_left;
}

/* Config-shared：settings 内部 setter - 右边距 */
static void printer_settings_set_margin_right(uint8_t margin_right)
{
    settings.margin_right = margin_right;
}

/* Config-shared：settings 内部 setter - 放大倍数
 * 当前策略：
 * - 最小 1
 * - 最大 3
 */
static void printer_settings_set_scale(uint8_t scale)
{
    if (scale < 1U) {
        scale = 1U;
    }
    if (scale > 3U) {
        scale = 3U;
    }

    settings.scale = scale;
}

/* 恢复打印相关默认状态：对应 ESC @ */
static void printer_reset_to_default(void)
{
    printer_clear_text_buffer("ESC @ reset");
    printer_execute_request = 0U;
    printer_settings_reset_default();
}

/* 统一打印命令帧日志 */
static void log_command_frame(const uint8_t *cmd, uint8_t len)
{
    uint8_t b0 = (len > 0U) ? cmd[0] : 0U;
    uint8_t b1 = (len > 1U) ? cmd[1] : 0U;
    uint8_t b2 = (len > 2U) ? cmd[2] : 0U;

    log_debug("ESC CMD: 0x%02X 0x%02X 0x%02X...\r\n", b0, b1, b2);
}

/* 解析 ESC a n 的参数：
 * 兼容二进制 0/1/2 以及 ASCII '0'/'1'/'2'
 * 成功返回 1，并将结果写入 out_val
 * 失败返回 0
 */
static uint8_t parse_alignment_value(uint8_t raw, uint8_t *out_val)
{
    if (out_val == NULL) {
        return 0U;
    }

    if (raw >= '0' && raw <= '2') {
        *out_val = (uint8_t)(raw - '0');
        return 1U;
    }

    if (raw <= 2U) {
        *out_val = raw;
        return 1U;
    }

    return 0U;
}

/* ========================= 对外接口 ========================= */

void printer_request_execute(void)
{
    printer_execute_request = 1U;
}

/* 兼容保留：旧接口
 * 当前建议 app_printer 不再直接调用它
 */
uint8_t printer_consume_execute_request(void)
{
    if (printer_execute_request != 0U) {
        printer_execute_request = 0U;
        return 1U;
    }
    return 0U;
}

const PrintSettings *printer_get_settings(void)
{
    return &settings;
}

/* 统一的打印执行入口
 * 当前仍保持 DEBUG 模式，便于观察字形
 */
void printer_execute_buffer(void)
{
    if (print_buffer_is_empty()) {
        log_error("print_buffer is empty\r\n");
        return;
    }

    log_info("Start printing the buffer...\r\n");

    /* 当前阶段保持 DEBUG 模式，方便观察字形 */
    dm_print_string_debug(print_buffer_get_data());

    /* 当前阶段保持原有行为：打印完成后清空缓冲区 */
    printer_clear_text_buffer("print finished");
}

/* 统一由命令层处理待执行打印请求
 *
 * 未来 FreeRTOS 视角：
 * - 这里是“打印执行任务”的天然候选入口之一
 * - 后续很适合从“轮询标志”演进成“等待打印事件”
 */
void printer_process_execute_request(void)
{
    if (printer_consume_execute_request()) {
        printer_execute_buffer();
    }
}

void handle_escpos_command(uint8_t *cmd, uint8_t len)
{
    if (cmd == NULL || len == 0U) {
        return;
    }

    log_command_frame(cmd, len);

    /* =========================================================
     * 第一类：demo 简化触发命令（当前阶段保留）
     * ========================================================= */

    /* 0A 00 -> 请求打印并额外输出一个 CRLF（demo 简化协议） */
    if (len >= 2U && cmd[0] == 0x0A && cmd[1] == 0x00) {
        printer_request_execute();
        log_printf("\r\n");
        return;
    }

    /* 0C 00 -> 请求打印（demo 简化协议） */
    if (len >= 2U && cmd[0] == 0x0C && cmd[1] == 0x00) {
        printer_request_execute();
        return;
    }

    /* =========================================================
     * 第二类：ESC 开头命令
     * ========================================================= */
    if (cmd[0] != 0x1B) {
        log_error("Unknown command frame: first byte is not ESC\r\n");
        return;
    }

    /* ESC @ -> 初始化打印机（清打印缓冲区，恢复默认模式） */
    if (len >= 2U && cmd[1] == 0x40) {
        printer_reset_to_default();
        log_info("ESC @ : printer reset to default\r\n");
        return;
    }

    /* ESC d n -> 向前走纸 n 行（当前 demo 用输出空行模拟） */
    if (len >= 3U && cmd[1] == 'd') {
        uint8_t n = cmd[2];
        log_info("ESC d %d (Feed %d lines)\r\n", n, n);

        for (uint8_t i = 0U; i < n; i++) {
            log_printf("\r\n");
        }
        return;
    }

    /* ESC a n -> 对齐（左 / 中 / 右）
     * 当前仍记录设置，但由于打印先恢复为 DEBUG 模式，
     * 此设置暂时不会体现在打印效果上。
     */
    if (len >= 3U && cmd[1] == 'a') {
        uint8_t val = 0U;

        if (!parse_alignment_value(cmd[2], &val)) {
            log_error("ESC a invalid n: %d\r\n", cmd[2]);
            return;
        }

        printer_settings_set_alignment(val);

        if (val == 0U) {
            log_info("Alignment is set to left (0)\r\n");
        } else if (val == 1U) {
            log_info("Alignment is set to center (1)\r\n");
        } else {
            log_info("Alignment is set to right (2)\r\n");
        }
        return;
    }

    /* ESC 3 n -> 行间距 */
    if (len >= 3U && cmd[1] == '3') {
        printer_settings_set_line_spacing(cmd[2]);
        log_info("The line spacing is set to %d\r\n", settings.line_spacing);
        return;
    }

    /* =========================================================
     * 第三类：当前 demo 自定义 ESC 命令
     * ========================================================= */

    /* ESC L n -> 当前 demo 自定义为左边距 */
    if (len >= 3U && cmd[1] == 0x4C) {
        printer_settings_set_margin_left(cmd[2]);
        log_info("The left margin is set to %d\r\n", settings.margin_left);
        return;
    }

    /* ESC r n -> 当前 demo 自定义为右边距 */
    if (len >= 3U && cmd[1] == 'r') {
        printer_settings_set_margin_right(cmd[2]);
        log_info("The right margin is set to %d\r\n", settings.margin_right);
        return;
    }

    /* ESC E n -> 当前 demo 自定义为放大倍数 */
    if (len >= 3U && cmd[1] == 0x45) {
        printer_settings_set_scale(cmd[2]);
        log_info("The magnification is set to %d\r\n", settings.scale);
        return;
    }

    /* ESC 1D -> 当前 demo 自定义“切纸模拟” */
    if (len >= 2U && cmd[1] == 0x1D) {
        log_info("Demo cut-paper simulation\r\n");
        dm_print_string_debug("________________________");
        return;
    }

    /* 未识别命令 */
    log_error("Unknown ESC command\r\n");
}
