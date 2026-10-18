/*
 * conformance.c - 接口套件的条款实现。
 *
 * 层级：test
 * 职责：驱动 stream/indicator/clock 实现通过每一条款。
 * 上下文：调用者线程；harness hook 必须能在该上下文调用。
 * 依赖：fw/conformance.h、stdio.h、string.h。
 */
#include "fw/conformance.h"
#include <stdio.h>
#include <string.h>

/* 紧跟在调用者 capacity 之后写入 canary，这样即使 fw_stream_read 会夹紧
 * count，端口越界也会被抓住。 */
#define FW_CONFORMANCE_CANARY 0xCDU

/* 容纳最大请求和 canary 余量。1 字节越界必须落在这个数组内，绝不能越过：
 * 套件用于发现故障，而不是制造故障。FW_CONFORMANCE_CAPACITY 因此严格小于
 * 缓冲区大小。 */
#define FW_CONFORMANCE_CAPACITY 16U
#define FW_CONFORMANCE_PATTERN 16U
#define FW_CONFORMANCE_BUFFER 32U

static void verdict(fw_conformance_result_t *result, const char *clause, bool held)
{
    ++result->checks;
    if (held) return;
    ++result->failed;
    if (result->failed_count < FW_CONFORMANCE_MAX_CLAUSES) {
        result->failed_clauses[result->failed_count++] = clause;
    }
    printf("FAIL %s\n", clause);
}

static void unsupported(fw_conformance_result_t *result, const char *clause)
{
    ++result->skipped;
    printf("SKIP %s (harness hook not supplied)\n", clause);
}

/* 读到端口报告没有数据为止，使“空”真正为空。
 * clamp 保证越界端口在测量时不会破坏本函数的栈帧。 */
static void drain_input(const fw_stream_t *device, uint8_t *buffer, size_t capacity)
{
    size_t count;
    unsigned round;
    if (capacity > FW_CONFORMANCE_CAPACITY) capacity = FW_CONFORMANCE_CAPACITY;
    for (round = 0U; round < 128U; ++round) {
        count = 0U;
        if (fw_stream_read(device, buffer, capacity, &count) != FW_OK) return;
        if (count == 0U) return;
    }
}

static void drain_output(const fw_conformance_stream_io_t *io, uint8_t *buffer, size_t capacity)
{
    unsigned round;
    for (round = 0U; round < 128U; ++round) {
        if (io->drain(io->ctx, buffer, capacity) == 0U) return;
    }
}

/* -------------------------------------------------------------------------
 * Stream 条款
 * ------------------------------------------------------------------------- */
fw_conformance_result_t fw_conformance_stream(const fw_stream_ops_t *ops, void *ctx,
                                              const fw_conformance_stream_io_t *io)
{
    fw_conformance_result_t result = {0};
    fw_stream_t device;
    uint8_t pattern[FW_CONFORMANCE_PATTERN];
    uint8_t reference[FW_CONFORMANCE_PATTERN];
    uint8_t buffer[FW_CONFORMANCE_BUFFER];
    size_t count;
    size_t moved;
    unsigned i;
    bool held;
    fw_status_t status;

    if (ops == NULL || ops->read == NULL || ops->write == NULL) {
        verdict(&result, "STREAM.OPS.COMPLETE", false);
        return result;
    }
    verdict(&result, "STREAM.OPS.COMPLETE", true);

    if (io == NULL || io->feed == NULL || io->drain == NULL) {
        unsupported(&result, "STREAM.* (needs feed+drain harness hooks)");
        return result;
    }
    device.ops = ops;
    device.ctx = ctx;

    for (i = 0U; i < FW_CONFORMANCE_PATTERN; ++i) pattern[i] = (uint8_t)(0xA0U + i);
    drain_output(io, buffer, sizeof(buffer));
    drain_input(&device, buffer, sizeof(buffer));

    /* --- read：空端口非阻塞，且不返回数据 --- */
    count = 123U;
    status = fw_stream_read(&device, buffer, 4U, &count);
    verdict(&result, "STREAM.READ.EMPTY", status == FW_EAGAIN && count == 0U);

    /* --- read：排入的数据按顺序返回 --- */
    moved = io->feed(io->ctx, pattern, FW_CONFORMANCE_PATTERN);
    if (moved != FW_CONFORMANCE_PATTERN) {
        verdict(&result, "STREAM.HARNESS.FEED", false);
    } else {
        memset(buffer, FW_CONFORMANCE_CANARY, sizeof(buffer));
        count = 0U;
        status = fw_stream_read(&device, buffer, FW_CONFORMANCE_PATTERN, &count);
        held = (status == FW_OK) && (count == FW_CONFORMANCE_PATTERN);
        for (i = 0U; held && i < FW_CONFORMANCE_PATTERN; ++i) held = buffer[i] == pattern[i];
        verdict(&result, "STREAM.READ.ORDER", held);
    }

    /* --- read：绝不写超过调用者 capacity --- */
    drain_input(&device, buffer, sizeof(buffer));
    moved = io->feed(io->ctx, pattern, FW_CONFORMANCE_PATTERN);
    if (moved != FW_CONFORMANCE_PATTERN) {
        verdict(&result, "STREAM.HARNESS.FEED", false);
    } else {
        memset(buffer, FW_CONFORMANCE_CANARY, sizeof(buffer));
        count = 0U;
        (void)fw_stream_read(&device, buffer, 4U, &count);
        held = count <= 4U;
        for (i = 4U; held && i < sizeof(buffer); ++i) held = buffer[i] == FW_CONFORMANCE_CANARY;
        verdict(&result, "STREAM.READ.BOUNDED", held);
    }

    /* --- read：已排空的端口不凭空产生数据 --- */
    drain_input(&device, buffer, sizeof(buffer));
    count = 123U;
    status = fw_stream_read(&device, buffer, 4U, &count);
    verdict(&result, "STREAM.READ.DRAINED", status == FW_EAGAIN && count == 0U);

    /* --- write：完整接受要如实、按顺序报告 --- */
    drain_output(io, buffer, sizeof(buffer));
    count = 123U;
    status = fw_stream_write(&device, pattern, FW_CONFORMANCE_PATTERN, &count, 0U);
    moved = io->drain(io->ctx, buffer, sizeof(buffer));
    held = (status == FW_OK) && (count == FW_CONFORMANCE_PATTERN) &&
           (moved == FW_CONFORMANCE_PATTERN) &&
           (memcmp(buffer, pattern, FW_CONFORMANCE_PATTERN) == 0);
    verdict(&result, "STREAM.WRITE.COUNT", held);

    /* --- write：绝不保留调用者缓冲区 --- */
    memcpy(reference, pattern, FW_CONFORMANCE_PATTERN);
    drain_output(io, buffer, sizeof(buffer));
    count = 0U;
    status = fw_stream_write(&device, pattern, FW_CONFORMANCE_PATTERN, &count, 0U);
    memset(pattern, 0x5AU, FW_CONFORMANCE_PATTERN); /* 若保留借用缓冲，这里已被破坏 */
    moved = io->drain(io->ctx, buffer, sizeof(buffer));
    held = (status == FW_OK) && (count == FW_CONFORMANCE_PATTERN) &&
           (moved == FW_CONFORMANCE_PATTERN) &&
           (memcmp(buffer, reference, FW_CONFORMANCE_PATTERN) == 0);
    verdict(&result, "STREAM.WRITE.NORETAIN", held);
    memcpy(pattern, reference, FW_CONFORMANCE_PATTERN);

    /* --- write：部分接受可按后缀重试 --- */
    if (io->set_write_limit == NULL) {
        unsupported(&result, "STREAM.WRITE.PARTIAL");
    } else {
        drain_output(io, buffer, sizeof(buffer));
        io->set_write_limit(io->ctx, 4U);
        count = 0U;
        status = fw_stream_write(&device, reference, FW_CONFORMANCE_PATTERN, &count, 0U);
        moved = io->drain(io->ctx, buffer, sizeof(buffer));
        held = (status == FW_OK) && (count == 4U) && (moved == 4U) &&
               (memcmp(buffer, reference, 4U) == 0);
        verdict(&result, "STREAM.WRITE.PARTIAL", held);
        io->set_write_limit(io->ctx, SIZE_MAX);
    }

    /* --- write：错误时仍报告真实进展 --- */
    if (io->fault_write == NULL) {
        unsupported(&result, "STREAM.WRITE.FAULT.PROGRESS");
    } else {
        drain_output(io, buffer, sizeof(buffer));
        io->fault_write(io->ctx, FW_ETIMEOUT);
        count = 123U;
        status = fw_stream_write(&device, reference, FW_CONFORMANCE_PATTERN, &count, 0U);
        moved = io->drain(io->ctx, buffer, sizeof(buffer));
        /* 契约允许失败但禁止沉默：count 必须说明接受了多少，
         * 且这些字节必须能取回。 */
        held = (status == FW_ETIMEOUT) && (count <= FW_CONFORMANCE_PATTERN) &&
               (moved == count) && (memcmp(buffer, reference, count) == 0);
        verdict(&result, "STREAM.WRITE.FAULT.PROGRESS", held);
    }

    /* --- read：流丢失时不返回数据 --- */
    if (io->fault_read == NULL) {
        unsupported(&result, "STREAM.READ.FAULT.NODATA");
        unsupported(&result, "STREAM.READ.FAULT.OVERFLOW");
    } else {
        io->fault_read(io->ctx, FW_EIO);
        count = 123U;
        status = fw_stream_read(&device, buffer, 4U, &count);
        verdict(&result, "STREAM.READ.FAULT.NODATA", status == FW_EIO && count == 0U);

        io->fault_read(io->ctx, FW_EOVERFLOW);
        count = 123U;
        status = fw_stream_read(&device, buffer, 4U, &count);
        verdict(&result, "STREAM.READ.FAULT.OVERFLOW", status == FW_EOVERFLOW && count == 0U);
    }

    return result;
}

/* -------------------------------------------------------------------------
 * Indicator 条款
 * ------------------------------------------------------------------------- */
fw_conformance_result_t fw_conformance_indicator(const fw_indicator_ops_t *ops, void *ctx,
                                                 const fw_conformance_indicator_io_t *io)
{
    fw_conformance_result_t result = {0};
    fw_indicator_t device;
    bool held;

    if (ops == NULL || ops->set == NULL) {
        verdict(&result, "INDICATOR.OPS.COMPLETE", false);
        return result;
    }
    verdict(&result, "INDICATOR.OPS.COMPLETE", true);
    device.ops = ops;
    device.ctx = ctx;

    held = fw_indicator_set(&device, true) == FW_OK;
    held = held && fw_indicator_set(&device, false) == FW_OK;
    verdict(&result, "INDICATOR.SET.OK", held);

    if (io == NULL || io->state == NULL) {
        unsupported(&result, "INDICATOR.STATE");
    } else {
        held = fw_indicator_set(&device, true) == FW_OK && io->state(io->ctx);
        held = held && fw_indicator_set(&device, false) == FW_OK && !io->state(io->ctx);
        verdict(&result, "INDICATOR.STATE", held);
    }

    if (io == NULL || io->fault_set == NULL || io->state == NULL) {
        unsupported(&result, "INDICATOR.FAULT.NOSTATE");
    } else {
        (void)fw_indicator_set(&device, true);
        io->fault_set(io->ctx, FW_EIO);
        /* 失败的请求不能让输出声称已经变成新状态。 */
        held = fw_indicator_set(&device, false) == FW_EIO && io->state(io->ctx);
        verdict(&result, "INDICATOR.FAULT.NOSTATE", held);
        io->fault_set(io->ctx, FW_OK);
    }

    return result;
}

/* -------------------------------------------------------------------------
 * Clock 条款
 * ------------------------------------------------------------------------- */
fw_conformance_result_t fw_conformance_clock(const fw_clock_t *clock)
{
    fw_conformance_result_t result = {0};
    uint32_t first;
    uint32_t second;

    if (clock == NULL || clock->now_ms == NULL) {
        verdict(&result, "CLOCK.PRESENT", false);
        return result;
    }
    verdict(&result, "CLOCK.PRESENT", true);

    first = clock->now_ms(clock->ctx);
    second = clock->now_ms(clock->ctx);
    /* 按 2^32 单调回绕：连续两次读取不能看起来倒退了超过半个量程。 */
    verdict(&result, "CLOCK.MONOTONIC",
            fw_elapsed_ms(second, first) < UINT32_C(0x80000000));
    return result;
}

bool fw_conformance_ok(const fw_conformance_result_t *result)
{
    return result != NULL && result->failed == 0U;
}
