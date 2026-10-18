/*
 * fw/ring.c - 固定容量字节 FIFO。
 *
 * 层级：framework/core
 * 职责：在调用者缓冲区和静态存储之间复制字节。
 * 上下文：无内部锁。UART 端口在任务侧访问时关闭中断，其他用户必须自行
 *         提供等价串行化。
 * 依赖：fw/ring.h。
 */
#include "fw/ring.h"

/* -------------------------------------------------------------------------
 * 公开 API
 * ------------------------------------------------------------------------- */
fw_status_t fw_ring_init(fw_ring_t *ring, uint8_t *storage, size_t capacity)
{
    if (ring == NULL || storage == NULL || capacity == 0U) return FW_EINVAL;
    ring->storage = storage;
    ring->capacity = capacity;
    fw_ring_clear(ring);
    return FW_OK;
}

void fw_ring_clear(fw_ring_t *ring)
{
    if (ring != NULL) {
        ring->head = 0U;
        ring->tail = 0U;
        ring->count = 0U;
    }
}

size_t fw_ring_write(fw_ring_t *ring, const uint8_t *data, size_t length)
{
    size_t done = 0U;
    if (ring == NULL || data == NULL || ring->storage == NULL) return 0U;
    /* 到达容量即停止并返回短计数；由调用者决定这是背压、溢出还是重试。 */
    while (done < length && ring->count < ring->capacity) {
        ring->storage[ring->head] = data[done++];
        ring->head = (ring->head + 1U) % ring->capacity;
        ++ring->count;
    }
    return done;
}

size_t fw_ring_read(fw_ring_t *ring, uint8_t *data, size_t capacity)
{
    size_t done = 0U;
    if (ring == NULL || data == NULL || ring->storage == NULL) return 0U;
    while (done < capacity && ring->count != 0U) {
        data[done++] = ring->storage[ring->tail];
        ring->tail = (ring->tail + 1U) % ring->capacity;
        --ring->count;
    }
    return done;
}

size_t fw_ring_peek(const fw_ring_t *ring, uint8_t *data, size_t capacity)
{
    size_t done = 0U;
    size_t index;
    if (ring == NULL || data == NULL || ring->storage == NULL) return 0U;
    /* 从 tail 开始遍历但不修改索引：DMA/IT 发送器在完成回调消费之前，
     * 必须保持被 peek 的 chunk 稳定。 */
    index = ring->tail;
    while (done < capacity && done < ring->count) {
        data[done++] = ring->storage[index];
        index = (index + 1U) % ring->capacity;
    }
    return done;
}
