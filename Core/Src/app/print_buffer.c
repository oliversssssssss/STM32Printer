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
 *    - 当前由打印执行链读取/取快照
 *
 * 2. g_print_buf_len
 *    - 保存当前缓冲有效长度
 *    - 只允许本模块内部维护
 *
 * 3. g_print_buffer_mutex
 *    - RTOS 下用于保护共享缓冲
 *    - 由 app_freertos 在运行时创建并绑定
 *
 * 当前阶段：
 * - 外部模块只能通过接口访问本缓冲
 * - 不允许绕过本模块直接改底层数组或长度
 * ============================================================ */

/* Task-shared：待打印文本缓冲，本模块私有持有 */
static char g_print_buffer[PRINT_BUFFER_SIZE];

/* Task-shared：缓冲有效长度，只允许本模块内部维护 */
static uint16_t g_print_buf_len = 0U;

/* RTOS 互斥锁：运行时绑定 */
static osMutexId_t g_print_buffer_mutex = NULL;

static uint8_t print_buffer_lock(uint32_t timeout)
{
    if ((g_print_buffer_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        return (osMutexAcquire(g_print_buffer_mutex, timeout) == osOK) ? 1U : 0U;
    }

    /* 内核未运行，或还未绑定 mutex，则直接通过 */
    return 1U;
}

static void print_buffer_unlock(void)
{
    if ((g_print_buffer_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        (void)osMutexRelease(g_print_buffer_mutex);
    }
}

void print_buffer_bind_mutex(osMutexId_t mutex_handle)
{
    g_print_buffer_mutex = mutex_handle;
}

void print_buffer_init(void)
{
    print_buffer_clear();
}

void print_buffer_clear(void)
{
    if (!print_buffer_lock(100)) {
        return;
    }

    memset(g_print_buffer, 0, sizeof(g_print_buffer));
    g_print_buf_len = 0U;

    print_buffer_unlock();
}

uint8_t print_buffer_append_text(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) {
        return 0U;
    }

    if (!print_buffer_lock(100)) {
        return 0U;
    }

    if ((uint32_t)g_print_buf_len + (uint32_t)len >= PRINT_BUFFER_SIZE) {
        print_buffer_unlock();
        return 0U;
    }

    memcpy(&g_print_buffer[g_print_buf_len], data, len);
    g_print_buf_len = (uint16_t)(g_print_buf_len + len);
    g_print_buffer[g_print_buf_len] = '\0';

    print_buffer_unlock();
    return 1U;
}

/* 兼容保留：
 * 返回内部缓冲首地址。
 * 注意：在 RTOS 路径下，不建议长期依赖这个指针做跨任务使用。
 * 打印路径应优先使用 print_buffer_take_snapshot_and_clear()。
 */
const char *print_buffer_get_data(void)
{
    return g_print_buffer;
}

uint16_t print_buffer_get_length(void)
{
    uint16_t len = 0U;

    if (!print_buffer_lock(100)) {
        return 0U;
    }

    len = g_print_buf_len;
    print_buffer_unlock();

    return len;
}

uint8_t print_buffer_is_empty(void)
{
    uint8_t empty = 1U;

    if (!print_buffer_lock(100)) {
        return 1U;
    }

    empty = (g_print_buf_len == 0U) ? 1U : 0U;
    print_buffer_unlock();

    return empty;
}

uint16_t print_buffer_take_snapshot_and_clear(char *out, uint16_t out_size)
{
    uint16_t copy_len = 0U;

    if (out == NULL || out_size == 0U) {
        return 0U;
    }

    if (!print_buffer_lock(100)) {
        return 0U;
    }

    if (g_print_buf_len == 0U) {
        out[0] = '\0';
        print_buffer_unlock();
        return 0U;
    }

    copy_len = g_print_buf_len;
    if (copy_len >= out_size) {
        copy_len = (uint16_t)(out_size - 1U);
    }

    memcpy(out, g_print_buffer, copy_len);
    out[copy_len] = '\0';

    /* 原子地清空共享缓冲 */
    memset(g_print_buffer, 0, sizeof(g_print_buffer));
    g_print_buf_len = 0U;

    print_buffer_unlock();

    return copy_len;
}
