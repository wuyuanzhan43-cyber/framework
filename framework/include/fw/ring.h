/*
 * fw/ring.h - 固定容量字节 FIFO。
 *
 * 层级：framework/core
 * 职责：为端口 RX/TX 路径和 Host 模拟提供共享存储原语。
 * 上下文：无内部同步；ISR/任务对必须自行串行化访问。
 *         不能只靠 volatile 代替这条规则。
 * 依赖：fw/status.h、stddef.h、stdint.h。
 */
#ifndef FW_RING_H
#define FW_RING_H

#include <stddef.h>
#include <stdint.h>
#include "fw/status.h"

typedef struct {
    uint8_t *storage;  /* 调用者拥有；生命周期必须覆盖 ring */
    size_t capacity;   /* 字节数；初始化后大于 0；不要求是 2 的幂 */
    size_t head;       /* 下一个写入位置，按 capacity 取模 */
    size_t tail;       /* 下一个读取位置，按 capacity 取模 */
    size_t count;      /* 当前存储字节数，范围 0..capacity */
} fw_ring_t;

fw_status_t fw_ring_init(fw_ring_t *ring, uint8_t *storage, size_t capacity);
void fw_ring_clear(fw_ring_t *ring);

/* 复制最多 length 字节，返回实际写入数；短计数表示 ring 已满。
 * data 仅在本次调用期间借用。 */
size_t fw_ring_write(fw_ring_t *ring, const uint8_t *data, size_t length);

/* 复制最多 capacity 字节，返回实际读取数。 */
size_t fw_ring_read(fw_ring_t *ring, uint8_t *data, size_t capacity);

/* 复制最多 capacity 字节但不消费。同步规则与 read/write 相同。
 * 用于 DMA/IT 发送器：缓冲区必须在传输完成前保持稳定。 */
size_t fw_ring_peek(const fw_ring_t *ring, uint8_t *data, size_t capacity);

#endif
