/*
 * fw/io.h - 与硬件无关的设备契约。
 *
 * 层级：framework/core
 * 职责：提供应用代码使用的 stream、indicator、clock 接口。
 * 上下文：设备由单个任务拥有；除非实现明确说明，否则禁止在 ISR 中调用。
 * 依赖：fw/status.h、stdbool.h、stddef.h、stdint.h。
 *
 * 本文件中的注释就是契约。端口由 tests/conformance 逐条验证；行为变化时
 * 必须同时修改契约并补回归测试。
 */
#ifndef FW_IO_H
#define FW_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw/status.h"

/* -------------------------------------------------------------------------
 * 指示器
 * ------------------------------------------------------------------------- */
typedef struct {
    /* 把逻辑开/关请求施加到输出设备。 */
    fw_status_t (*set)(void *ctx, bool on);
} fw_indicator_ops_t;

typedef struct {
    const fw_indicator_ops_t *ops;  /* 静态函数表；生命周期必须覆盖 ctx */
    void *ctx;                      /* 实现私有的设备上下文 */
} fw_indicator_t;

/* set 成功只表示“已记录输出请求”，不代表 LED 的实际光学反馈。
 * 除非实现自己提供同步，否则只允许单调用者。 */
fw_status_t fw_indicator_set(const fw_indicator_t *device, bool on);

/* -------------------------------------------------------------------------
 * 字节流
 * ------------------------------------------------------------------------- */
typedef struct {
    fw_status_t (*read)(void *ctx, uint8_t *data, size_t capacity,
                        size_t *count);
    fw_status_t (*write)(void *ctx, const uint8_t *data, size_t length,
                         size_t *count, uint32_t timeout_ms);
} fw_stream_ops_t;

typedef struct {
    const fw_stream_ops_t *ops;  /* 静态函数表；生命周期必须覆盖 ctx */
    void *ctx;                   /* 实现私有的设备上下文 */
} fw_stream_t;

/*
 * read：非阻塞；没有数据时返回 EAGAIN 且 count=0；数据复制给调用者。
 * 实现绝不能写超过 capacity 字节；包装层在错误实现返回后无法撤销越界。
 * EOVERFLOW/EIO 表示流连续性丢失：不返回数据，调用者应复位 parser。
 *
 * write：同步返回；返回后不保留调用者缓冲区。
 * count = 按顺序真正接受的字节数，超时/错误时也必须报告；调用者只重试后缀。
 * 实现必须在每次返回时报告真实进展。如果错误后无法知道进展，该后端就不能
 * 被本接口安全表达；接受了字节却报 0 属于违约。
 * accepted 不承诺物理线路已经发完；count=0 表示没有进展。
 *
 * 单任务拥有；禁止在 ISR 中调用。零长度操作成功。
 * 设备/ops/ctx 的生命周期必须覆盖所有使用期。包装层不做内存分配。
 */
fw_status_t fw_stream_read(const fw_stream_t *device, uint8_t *data,
                           size_t capacity, size_t *count);
fw_status_t fw_stream_write(const fw_stream_t *device, const uint8_t *data,
                            size_t length, size_t *count, uint32_t timeout_ms);

/* -------------------------------------------------------------------------
 * 时钟
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t (*now_ms)(void *ctx);  /* 按 2^32 回绕的单调毫秒时钟；不能为 NULL */
    void *ctx;
} fw_clock_t;

/* 单调、按 2^32 回绕；两次读取的时间差必须小于 2^31 ms。 */
uint32_t fw_elapsed_ms(uint32_t now, uint32_t then);

#endif
