#ifndef INC_RECEIPT_JOB_BUFFER_H_
#define INC_RECEIPT_JOB_BUFFER_H_

#include <stdint.h>
#include "cmsis_os2.h"
#include "print_settings.h"

/*
 * 一张票内部的单个样式片段
 * text_pool[text_start ... text_start + text_len) 对应本段文本
 * settings 对应本段样式
 */
typedef struct {
    uint16_t text_start;
    uint16_t text_len;
    PrintSettings settings;
} ReceiptSegment;

/*
 * 一张完整票据任务
 * text_pool 保存整张票的原始文本池
 * segments 保存每段文本在 text_pool 中的位置 + 样式
 */
#define RECEIPT_JOB_TEXT_POOL_SIZE   4096U
#define RECEIPT_JOB_MAX_SEGMENTS     64U

typedef struct {
    char text_pool[RECEIPT_JOB_TEXT_POOL_SIZE];
    uint16_t text_len;

    ReceiptSegment segments[RECEIPT_JOB_MAX_SEGMENTS];
    uint16_t segment_count;
} ReceiptJob;

/* ---------- 对 ReceiptJob 结构本身的操作 ---------- */

/* 初始化 / 清空一个 job 结构 */
void receipt_job_init(ReceiptJob *job);

/* 判断一个 job 是否为空 */
uint8_t receipt_job_is_empty(const ReceiptJob *job);

/* 向 job 追加一个 segment 文本片段 */
uint8_t receipt_job_append_segment(ReceiptJob *job,
                                   const char *text,
                                   uint16_t len,
                                   const PrintSettings *settings);

/* ---------- 共享 pending job 缓冲区操作 ---------- */

/*
 * 绑定互斥锁
 * 当前第一版允许不绑定；如果不绑定，则内部不加锁
 * 后续真正接入运行链时，可复用已有 mutex
 */
void receipt_job_buffer_bind_mutex(osMutexId_t mutex_handle);

/* 初始化共享 job 缓冲 */
void receipt_job_buffer_init(void);

/*
 * 存入一张待打印票据
 * 成功返回 1，失败返回 0
 * 当前策略：如果已有未消费 job，则拒绝覆盖
 */
uint8_t receipt_job_buffer_store(const ReceiptJob *job);

/*
 * 原子地取出一张 job 并清空共享缓冲
 * 成功返回 1，失败返回 0
 */
uint8_t receipt_job_buffer_take_snapshot_and_clear(ReceiptJob *out);

/* 查询是否已有待处理的 job */
uint8_t receipt_job_buffer_has_pending_job(void);

/* 清空共享 job 缓冲 */
void receipt_job_buffer_clear(void);

#endif /* INC_RECEIPT_JOB_BUFFER_H_ */
