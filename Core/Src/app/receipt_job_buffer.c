/*
 * receipt_job_buffer.c
 *
 *  Created on: 2026年3月24日
 *      Author: Administrator
 */

#include "receipt_job_buffer.h"
#include <string.h>

/* =========================================================
 * receipt_job_buffer.c
 *
 * 当前作用：
 * 1. 提供一张完整票据任务 ReceiptJob 的结构定义与操作
 * 2. 提供一个“共享待打印 job 缓冲区”
 *
 * 第一版策略：
 * - 共享缓冲区只允许同时挂 1 张待处理票据
 * - 若上一张票尚未被消费，则新票存储失败
 * - mutex 当前可选，不绑定也能工作
 * - 本阶段仅新增，不接入现有旧打印链
 * ========================================================= */

/* 共享 pending job */
static ReceiptJob g_pending_job;
static uint8_t g_has_pending_job = 0U;

/* 共享 mutex（可选） */
static osMutexId_t g_receipt_job_mutex = NULL;

/* ========================= 内部辅助 ========================= */

static uint8_t receipt_job_buffer_lock(uint32_t timeout)
{
    if ((g_receipt_job_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        return (osMutexAcquire(g_receipt_job_mutex, timeout) == osOK) ? 1U : 0U;
    }

    /* 未绑定 mutex 或 RTOS 未运行，直接通过 */
    return 1U;
}

static void receipt_job_buffer_unlock(void)
{
    if ((g_receipt_job_mutex != NULL) && (osKernelGetState() == osKernelRunning)) {
        (void)osMutexRelease(g_receipt_job_mutex);
    }
}

/* ========================= ReceiptJob 结构操作 ========================= */

void receipt_job_init(ReceiptJob *job)
{
    if (job == NULL) {
        return;
    }

    memset(job, 0, sizeof(ReceiptJob));
}

uint8_t receipt_job_is_empty(const ReceiptJob *job)
{
    if (job == NULL) {
        return 1U;
    }

    return (job->segment_count == 0U || job->text_len == 0U) ? 1U : 0U;
}

uint8_t receipt_job_append_segment(ReceiptJob *job,
                                   const char *text,
                                   uint16_t len,
                                   const PrintSettings *settings)
{
    ReceiptSegment *seg = NULL;

    if (job == NULL || text == NULL || settings == NULL || len == 0U) {
        return 0U;
    }

    if (job->segment_count >= RECEIPT_JOB_MAX_SEGMENTS) {
        return 0U;
    }

    if ((uint32_t)job->text_len + (uint32_t)len > RECEIPT_JOB_TEXT_POOL_SIZE) {
        return 0U;
    }

    seg = &job->segments[job->segment_count];

    seg->text_start = job->text_len;
    seg->text_len   = len;
    seg->settings   = *settings;

    memcpy(&job->text_pool[job->text_len], text, len);
    job->text_len = (uint16_t)(job->text_len + len);

    job->segment_count++;

    return 1U;
}

/* ========================= 共享 pending job 缓冲操作 ========================= */

void receipt_job_buffer_bind_mutex(osMutexId_t mutex_handle)
{
    g_receipt_job_mutex = mutex_handle;
}

void receipt_job_buffer_init(void)
{
    if (!receipt_job_buffer_lock(100U)) {
        return;
    }

    receipt_job_init(&g_pending_job);
    g_has_pending_job = 0U;

    receipt_job_buffer_unlock();
}

uint8_t receipt_job_buffer_store(const ReceiptJob *job)
{
    if (job == NULL || receipt_job_is_empty(job)) {
        return 0U;
    }

    if (!receipt_job_buffer_lock(100U)) {
        return 0U;
    }

    /* 第一版策略：已有待处理 job 时拒绝覆盖 */
    if (g_has_pending_job != 0U) {
        receipt_job_buffer_unlock();
        return 0U;
    }

    g_pending_job = *job;
    g_has_pending_job = 1U;

    receipt_job_buffer_unlock();
    return 1U;
}

uint8_t receipt_job_buffer_take_snapshot_and_clear(ReceiptJob *out)
{
    if (out == NULL) {
        return 0U;
    }

    if (!receipt_job_buffer_lock(100U)) {
        return 0U;
    }

    if (g_has_pending_job == 0U) {
        receipt_job_init(out);
        receipt_job_buffer_unlock();
        return 0U;
    }

    *out = g_pending_job;

    receipt_job_init(&g_pending_job);
    g_has_pending_job = 0U;

    receipt_job_buffer_unlock();
    return 1U;
}

uint8_t receipt_job_buffer_has_pending_job(void)
{
    uint8_t has_job = 0U;

    if (!receipt_job_buffer_lock(100U)) {
        return 0U;
    }

    has_job = g_has_pending_job;

    receipt_job_buffer_unlock();
    return has_job;
}

void receipt_job_buffer_clear(void)
{
    if (!receipt_job_buffer_lock(100U)) {
        return;
    }

    receipt_job_init(&g_pending_job);
    g_has_pending_job = 0U;

    receipt_job_buffer_unlock();
}
