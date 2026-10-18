/*
 * fw/host.h - 供框架测试使用的 PC 侧模拟设备。
 *
 * 层级：port/host
 * 职责：用内存 ring 模拟 UART/LED/时钟，并支持故障注入。
 * 上下文：仅测试线程，无并发。
 * 依赖：fw/io.h、fw/ring.h。
 */
#ifndef FW_HOST_H
#define FW_HOST_H

#include "fw/io.h"
#include "fw/ring.h"

#define FW_HOST_BUFFER_SIZE 1024U

typedef struct {
    fw_ring_t rx;                    /* 等待应用读取的字节 */
    fw_ring_t tx;                    /* 应用写出的字节，供测试检查 */
    uint8_t rx_storage[FW_HOST_BUFFER_SIZE];
    uint8_t tx_storage[FW_HOST_BUFFER_SIZE];
    uint32_t now_ms;                 /* 手动推进的假时钟 */
    size_t write_limit;              /* 单次 write 最多接受的字节数 */
    fw_status_t read_fault;          /* 一次性读取故障；FW_OK 表示正常 */
    fw_status_t write_fault;         /* 一次性写入故障；FW_OK 表示正常 */
    fw_status_t indicator_fault;     /* 持久的 indicator 故障 */
    bool led_on;                     /* 最后一次逻辑 LED 状态 */
    uint32_t led_writes;             /* 成功的 indicator 写入次数 */
} fw_host_t;

/* 复位 ring、时钟、故障和计数器。 */
void fw_host_init(fw_host_t *host);

/* 设备视图；host 上下文生命周期必须覆盖它们。 */
fw_stream_t fw_host_stream(fw_host_t *host);
fw_indicator_t fw_host_indicator(fw_host_t *host);
fw_clock_t fw_host_clock(fw_host_t *host);

/* 为应用的下一次 read 排队字节。RX ring 满时丢弃剩余部分，
 * 并设置一次性 EOVERFLOW。 */
size_t fw_host_receive(fw_host_t *host, const uint8_t *data, size_t length);

/* 取走应用之前写出的字节。 */
size_t fw_host_take_tx(fw_host_t *host, uint8_t *data, size_t capacity);

#endif
