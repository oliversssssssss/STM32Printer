#include "print_buffer.h"
#include <string.h>

/* ========================= 状态归属说明 =========================
 *
 * 【任务共享状态组 / Task-shared state】
 * 本模块内部维护打印文本缓冲：
 *
 * 1. g_print_buffer
 *    - 保存待打印的文本内容
 *    - 当前由接收处理链写入
 *    - 当前由打印执行链读取
 *
 * 2. g_print_buf_len
 *    - 保存当前缓冲有效长度
 *    - 只允许本模块内部维护
 *
 * 当前阶段：
 * - 外部模块只能通过接口访问本缓冲
 * - 不允许绕过本模块直接改底层数组或长度
 *
 * 未来 FreeRTOS 视角：
 * - 这是典型的 task-shared 业务对象
 * - 未来若接收任务与打印任务并行化，
 *   这里很可能是 mutex / 临界区保护的候选点
 * ============================================================ */

/* Task-shared：待打印文本缓冲，本模块私有持有 */
static char g_print_buffer[PRINT_BUFFER_SIZE];

/* Task-shared：缓冲有效长度，只允许本模块内部维护 */
static uint16_t g_print_buf_len = 0U;

void print_buffer_init(void)
{
    print_buffer_clear();
}

void print_buffer_clear(void)
{
    memset(g_print_buffer, 0, sizeof(g_print_buffer));
    g_print_buf_len = 0U;
}

uint8_t print_buffer_append_text(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) {
        return 0U;
    }

    if ((uint32_t)g_print_buf_len + (uint32_t)len >= PRINT_BUFFER_SIZE) {
        return 0U;
    }

    memcpy(&g_print_buffer[g_print_buf_len], data, len);
    g_print_buf_len = (uint16_t)(g_print_buf_len + len);
    g_print_buffer[g_print_buf_len] = '\0';

    return 1U;
}

const char *print_buffer_get_data(void)
{
    return g_print_buffer;
}

uint16_t print_buffer_get_length(void)
{
    return g_print_buf_len;
}

uint8_t print_buffer_is_empty(void)
{
    return (g_print_buf_len == 0U) ? 1U : 0U;
}