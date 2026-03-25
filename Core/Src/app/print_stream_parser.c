/*
 * print_stream_parser.c
 *
 *  Created on: 2026年3月24日
 *      Author: Administrator
 */

#include "print_stream_parser.h"

#include <string.h>
#include "receipt_job_buffer.h"
#include "escpos_commands.h"

/*
 * Step3 设计目标：
 * - 先把 parser 模块建起来
 * - 但还不接入运行主链
 * - 后续真正接入时，再由 app_printer_process_rx() 喂数据
 *
 * 当前支持的最小命令子集：
 *   ESC @        -> 重置当前票据状态
 *   ESC a n      -> alignment
 *   ESC 3 n      -> line spacing
 *   ESC L n      -> 当前阶段先沿用 legacy 的“左边距”语义
 *   ESC r n      -> 右边距
 *   ESC E n      -> 当前阶段先沿用 legacy 的“scale”语义
 *   FF (0x0C)    -> 提交当前票据任务并请求打印
 *
 * 当前阶段说明：
 * - parser 仅作为模块存在，不接入旧接收链
 * - 因此不会影响当前可运行的 legacy step 打印
 * - 等后续第四/第五步再接入 Receive/app_printer 主流程
 */

#define PARSER_RUN_BUFFER_SIZE   256U

typedef struct {
    PrintParserState state;

    /* 当前样式 */
    PrintSettings current_settings;

    /* 当前连续文本片段 */
    char current_run[PARSER_RUN_BUFFER_SIZE];
    uint16_t current_run_len;

    /* ESC 等待参数时暂存命令 */
    uint8_t pending_cmd;

    /* 当前正在组装的一张票 */
    ReceiptJob working_job;
} PrintStreamParser;

static PrintStreamParser g_parser;

/* ========================= 内部辅助 ========================= */

static void parser_reset_ticket_state(void)
{
    receipt_job_init(&g_parser.working_job);
    printer_settings_reset_struct(&g_parser.current_settings);

    g_parser.current_run_len = 0U;
    g_parser.pending_cmd = 0U;
}

static uint8_t parser_flush_current_run(void)
{
    if (g_parser.current_run_len == 0U) {
        return 1U;
    }

    if (!receipt_job_append_segment(&g_parser.working_job,
                                    g_parser.current_run,
                                    g_parser.current_run_len,
                                    &g_parser.current_settings)) {
        g_parser.current_run_len = 0U;
        return 0U;
    }

    g_parser.current_run_len = 0U;
    return 1U;
}

static void parser_append_text_byte(uint8_t b)
{
    /* 若当前片段满了，则先 flush 成一个 segment */
    if (g_parser.current_run_len >= PARSER_RUN_BUFFER_SIZE) {
        if (!parser_flush_current_run()) {
            /* flush 失败则直接丢弃后续字节，等待后续 reset */
            return;
        }
    }

    g_parser.current_run[g_parser.current_run_len++] = (char)b;
}

static void parser_handle_style_cmd(uint8_t cmd, uint8_t arg)
{
    switch (cmd) {
    case 'a':
        printer_settings_apply_alignment(&g_parser.current_settings, arg);
        break;

    case '3':
        printer_settings_apply_line_spacing(&g_parser.current_settings, arg);
        break;

    case 'L':
        /* 当前阶段先沿用 legacy demo：ESC L n -> 左边距 */
        printer_settings_apply_margin_left(&g_parser.current_settings, arg);
        break;

    case 'r':
        printer_settings_apply_margin_right(&g_parser.current_settings, arg);
        break;

    case 'E':
        /* 当前阶段先沿用 legacy demo：ESC E n -> scale */
        printer_settings_apply_scale(&g_parser.current_settings, arg);
        break;

    default:
        /* 其余命令当前阶段暂不处理 */
        break;
    }
}

static void parser_finalize_and_submit_job(void)
{
    /* 先把最后一段文本片段刷入 working_job */
    (void)parser_flush_current_run();

    if (receipt_job_is_empty(&g_parser.working_job)) {
        parser_reset_ticket_state();
        g_parser.state = PARSER_TEXT;
        return;
    }

    /* 当前阶段先允许覆盖提交失败后直接丢弃，避免把 parser 卡死 */
    if (receipt_job_buffer_store(&g_parser.working_job)) {
        printer_request_execute();
    }

    parser_reset_ticket_state();
    g_parser.state = PARSER_TEXT;
}

static uint8_t parser_is_text_byte(uint8_t b)
{
    /* 当前阶段仅接受 ASCII 可打印字符 + '\n' + '\t' */
    if (b == '\n' || b == '\t') {
        return 1U;
    }

    if (b >= 0x20U && b <= 0x7EU) {
        return 1U;
    }

    return 0U;
}

/* ========================= 对外接口 ========================= */

void print_stream_parser_init(void)
{
    memset(&g_parser, 0, sizeof(g_parser));
    g_parser.state = PARSER_TEXT;
    parser_reset_ticket_state();
}

void print_stream_parser_reset(void)
{
    memset(&g_parser, 0, sizeof(g_parser));
    g_parser.state = PARSER_TEXT;
    parser_reset_ticket_state();
}

void print_stream_parser_feed_byte(uint8_t b)
{
    switch (g_parser.state) {
    case PARSER_TEXT:
        /* ESC -> 进入命令解析 */
        if (b == 0x1BU) {
            (void)parser_flush_current_run();
            g_parser.state = PARSER_ESC_WAIT_CMD;
            return;
        }

        /* FF -> 提交当前票据并请求打印 */
        if (b == 0x0CU) {
            parser_finalize_and_submit_job();
            return;
        }

        /* 忽略 CR 和 NUL */
        if (b == '\r' || b == 0x00U) {
            return;
        }

        if (parser_is_text_byte(b)) {
            parser_append_text_byte(b);
        }
        return;

    case PARSER_ESC_WAIT_CMD:
        if (b == '@') {
            parser_reset_ticket_state();
            g_parser.state = PARSER_TEXT;
            return;
        }

        if (b == 'a' || b == '3' || b == 'L' || b == 'r' || b == 'E') {
            g_parser.pending_cmd = b;
            g_parser.state = PARSER_ESC_WAIT_ARG;
            return;
        }

        /* 未支持命令：直接回 TEXT */
        g_parser.state = PARSER_TEXT;
        return;

    case PARSER_ESC_WAIT_ARG:
        parser_handle_style_cmd(g_parser.pending_cmd, b);
        g_parser.pending_cmd = 0U;
        g_parser.state = PARSER_TEXT;
        return;

    default:
        g_parser.state = PARSER_TEXT;
        return;
    }
}

void print_stream_parser_feed_chunk(const uint8_t *buf, uint16_t len)
{
    uint16_t i = 0U;

    if (buf == NULL || len == 0U) {
        return;
    }

    for (i = 0U; i < len; i++) {
        print_stream_parser_feed_byte(buf[i]);
    }
}
