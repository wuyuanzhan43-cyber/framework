/*
 * fw/conformance.h - 可机器检查的接口一致性套件。
 *
 * 层级：test（Host 与目标）
 * 职责：驱动端口逐条通过 fw_stream/fw_indicator/fw_clock 契约。
 * 上下文：调用者线程；hook 必须能在同一上下文调用。
 * 依赖：fw/io.h、stdbool.h、stddef.h、stdint.h。
 */
#ifndef FW_CONFORMANCE_H
#define FW_CONFORMANCE_H

/* 接口一致性套件。
 *
 * framework/include/fw/io.h 规定了设备实现必须做什么。此前那些只是散文：
 * 端口作者无法检查自己的实现是否满足，reviewer 也无法区分“正确端口”和
 * “看起来很合理的端口”。
 *
 * 本套件把契约中可机器检查的部分变成可运行的代码，并明确列出无法自动化的
 * 部分，交给人工签核（见 docs/porting.md）。
 *
 * 套件不知道任何具体端口。端口提供 `..._io_t` hook：送入字节、取出字节、
 * 注入故障、限制写入。hook 为 NULL 不会导致失败——依赖它的条款计为 SKIP，
 * 这样不能注入故障的端口仍能得到有意义的结果，而不是假通过或假失败。
 *
 * 用法：
 *     fw_conformance_result_t r = fw_conformance_stream(&amp;ops, ctx, &amp;io);
 *     if (r.failed != 0U) { report(r.failed_clauses, r.failed_count); }
 *
 * 人类可读 trace 依赖 printf；结果结构体是自描述的，目标构建也可以不用
 * stdio 自行上报。 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw/io.h"

/* 上报失败条款的数量上限，不是契约条款总数。 */
#define FW_CONFORMANCE_MAX_CLAUSES 12U

typedef struct {
    unsigned checks;   /* 实际执行的条款数 */
    unsigned failed;   /* 未通过的条款数 */
    unsigned skipped;  /* 因缺少 harness hook 而未执行的条款数 */
    const char *failed_clauses[FW_CONFORMANCE_MAX_CLAUSES];
    unsigned failed_count;
} fw_conformance_result_t;

/* 端口提供的 hook，让套件可以驱动它。除 op-table 检查外，feed 和 drain 必填，
 * 其余可选。 */
typedef struct {
    /* 把最多 length 字节排入实现，返回排入字节数。
     * 短返回表示 harness 自身已满；套件会把受影响条款判为失败，
     * 而不是静默测试一个更小的用例。 */
    size_t (*feed)(void *ctx, const uint8_t *data, size_t length);
    /* 按顺序取回实现已接受输出的最多 capacity 字节，返回实际取回数。 */
    size_t (*drain)(void *ctx, uint8_t *data, size_t capacity);
    /* 让下一次 read/write 返回 status；FW_OK 清除注入。
     * NULL 表示依赖故障注入的条款报告 SKIP。 */
    void (*fault_read)(void *ctx, fw_status_t status);
    void (*fault_write)(void *ctx, fw_status_t status);
    /* 限制下一次 write 最多接受多少字节；SIZE_MAX 恢复无限。
     * NULL 表示部分写入条款报告 SKIP。 */
    void (*set_write_limit)(void *ctx, size_t max_bytes);
    void *ctx;
} fw_conformance_stream_io_t;

typedef struct {
    /* 报告 indicator 当前被命令的状态。 */
    bool (*state)(void *ctx);
    /* 让下一次 set() 返回 status；NULL 表示该条款 SKIP。 */
    void (*fault_set)(void *ctx, fw_status_t status);
    void *ctx;
} fw_conformance_indicator_io_t;

/* io 可以为 NULL；此时只运行不需要 harness hook 的条款。 */
fw_conformance_result_t fw_conformance_stream(const fw_stream_ops_t *ops, void *ctx,
                                              const fw_conformance_stream_io_t *io);
fw_conformance_result_t fw_conformance_indicator(const fw_indicator_ops_t *ops, void *ctx,
                                                 const fw_conformance_indicator_io_t *io);
fw_conformance_result_t fw_conformance_clock(const fw_clock_t *clock);

/* 便捷函数：没有失败时返回 true。SKIP 不是失败——它表示端口必须人工确认，
 * docs/porting.md 列出了这些条款。 */
bool fw_conformance_ok(const fw_conformance_result_t *result);

#endif
