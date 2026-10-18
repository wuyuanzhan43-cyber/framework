/*
 * fw/host/host.c - Host 测试使用的内存设备。
 *
 * 层级：port/host
 * 职责：模拟 UART ring、LED 状态、假时间和一次性故障。
 * 上下文：仅测试线程；按设计不加锁。
 * 依赖：fw/host.h、string.h。
 */
#include "fw/host.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * 流操作
 * ------------------------------------------------------------------------- */
static fw_status_t read_bytes(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    fw_host_t *host = ctx;
    /* 故障注入是一次性的：在这里消费它，下一次 read 就可以测试恢复，
     * 不需要测试侧重新初始化。 */
    if (host->read_fault != FW_OK) {
        fw_status_t error = host->read_fault;
        host->read_fault = FW_OK;
        fw_ring_clear(&host->rx);
        *count = 0U;
        return error;
    }
    *count = fw_ring_read(&host->rx, data, capacity);
    return *count == 0U ? FW_EAGAIN : FW_OK;
}

static fw_status_t write_bytes(void *ctx, const uint8_t *data, size_t length,
                               size_t *count, uint32_t timeout_ms)
{
    fw_host_t *host = ctx;
    fw_status_t error = host->write_fault;
    (void)timeout_ms;
    host->write_fault = FW_OK;
    /* write_limit 模拟较短的硬件 FIFO；即使注入故障，也要报告实际复制的
     * 字节数，符合 io.h 的进展契约。 */
    if (length > host->write_limit) length = host->write_limit;
    *count = fw_ring_write(&host->tx, data, length);
    if (error != FW_OK) return error;
    return *count == 0U ? FW_EAGAIN : FW_OK;
}

static fw_status_t set_indicator(void *ctx, bool on)
{
    fw_host_t *host = ctx;
    if (host->indicator_fault != FW_OK) return host->indicator_fault;
    host->led_on = on;
    ++host->led_writes;
    return FW_OK;
}

static uint32_t now_ms(void *ctx)
{
    return ((fw_host_t *)ctx)->now_ms;
}

static const fw_stream_ops_t stream_ops = { read_bytes, write_bytes };
static const fw_indicator_ops_t indicator_ops = { set_indicator };

/* -------------------------------------------------------------------------
 * 工厂函数与测试辅助
 * ------------------------------------------------------------------------- */
void fw_host_init(fw_host_t *host)
{
    memset(host, 0, sizeof(*host));
    (void)fw_ring_init(&host->rx, host->rx_storage, sizeof(host->rx_storage));
    (void)fw_ring_init(&host->tx, host->tx_storage, sizeof(host->tx_storage));
    host->write_limit = FW_HOST_BUFFER_SIZE;
}

fw_stream_t fw_host_stream(fw_host_t *host)
{
    const fw_stream_t device = { &stream_ops, host };
    return device;
}

fw_indicator_t fw_host_indicator(fw_host_t *host)
{
    const fw_indicator_t device = { &indicator_ops, host };
    return device;
}

fw_clock_t fw_host_clock(fw_host_t *host)
{
    const fw_clock_t clock = { now_ms, host };
    return clock;
}

size_t fw_host_receive(fw_host_t *host, const uint8_t *data, size_t length)
{
    size_t written = fw_ring_write(&host->rx, data, length);
    /* 截断被建模为流丢失：下一次 read 返回 EOVERFLOW 并清空 ring，
     * 与 UART 端口 RX 溢出时的行为一致。 */
    if (written != length) host->read_fault = FW_EOVERFLOW;
    return written;
}

size_t fw_host_take_tx(fw_host_t *host, uint8_t *data, size_t capacity)
{
    return fw_ring_read(&host->tx, data, capacity);
}
