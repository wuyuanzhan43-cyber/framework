/*
 * host_adapter.c - 把 Host 测试旋钮映射到一致性 hook。
 *
 * 层级：test
 * 职责：feed/drain/fault 适配；不含生产逻辑。
 * 上下文：仅测试线程。
 * 依赖：host_adapter.h。
 */
#include "host_adapter.h"

/* -------------------------------------------------------------------------
 * 字节流 hook
 * ------------------------------------------------------------------------- */

static size_t host_feed(void *ctx, const uint8_t *data, size_t length)
{
    return fw_host_receive((fw_host_t *)ctx, data, length);
}

static size_t host_drain(void *ctx, uint8_t *data, size_t capacity)
{
    return fw_host_take_tx((fw_host_t *)ctx, data, capacity);
}

static void host_fault_read(void *ctx, fw_status_t status)
{
    ((fw_host_t *)ctx)->read_fault = status;
}

static void host_fault_write(void *ctx, fw_status_t status)
{
    ((fw_host_t *)ctx)->write_fault = status;
}

static void host_set_write_limit(void *ctx, size_t max_bytes)
{
    ((fw_host_t *)ctx)->write_limit = max_bytes;
}

/* -------------------------------------------------------------------------
 * 指示器 hook
 * ------------------------------------------------------------------------- */

static bool host_indicator_state(void *ctx)
{
    return ((fw_host_t *)ctx)->led_on;
}

static void host_fault_indicator(void *ctx, fw_status_t status)
{
    ((fw_host_t *)ctx)->indicator_fault = status;
}

/* -------------------------------------------------------------------------
 * 工厂函数
 * ------------------------------------------------------------------------- */

fw_conformance_stream_io_t host_conformance_stream_io(fw_host_t *host)
{
    const fw_conformance_stream_io_t io = {
        host_feed, host_drain, host_fault_read, host_fault_write,
        host_set_write_limit, host
    };
    return io;
}

fw_conformance_indicator_io_t host_conformance_indicator_io(fw_host_t *host)
{
    const fw_conformance_indicator_io_t io = { host_indicator_state, host_fault_indicator, host };
    return io;
}
