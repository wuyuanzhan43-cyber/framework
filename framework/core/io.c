/*
 * fw/io.c - fw/io.h 的包装实现。
 *
 * 层级：framework/core
 * 职责：校验设备函数表，并夹紧端口返回的 count。
 * 上下文：调用者上下文；包装层不增加锁。
 * 依赖：fw/io.h。
 */
#include "fw/io.h"

/* -------------------------------------------------------------------------
 * 状态名
 * ------------------------------------------------------------------------- */
const char *fw_status_name(fw_status_t status)
{
    switch (status) {
    case FW_OK: return "OK";
    case FW_EINVAL: return "INVALID";
    case FW_EAGAIN: return "AGAIN";
    case FW_EIO: return "IO";
    case FW_ETIMEOUT: return "TIMEOUT";
    case FW_EOVERFLOW: return "OVERFLOW";
    case FW_ENOTREADY: return "NOT_READY";
    case FW_ENOTSUP: return "NOT_SUPPORTED";
    default: return "UNKNOWN";
    }
}

/* -------------------------------------------------------------------------
 * 指示器
 * ------------------------------------------------------------------------- */
fw_status_t fw_indicator_set(const fw_indicator_t *device, bool on)
{
    if (device == NULL || device->ops == NULL || device->ops->set == NULL) {
        return FW_EINVAL;
    }
    return device->ops->set(device->ctx, on);
}

/* -------------------------------------------------------------------------
 * 流包装层
 *
 * 包装层只做防御性校验，不承载策略：检查函数表、在失败时规范 count，
 * 并拒绝多报进展的端口。
 * ------------------------------------------------------------------------- */
fw_status_t fw_stream_read(const fw_stream_t *device, uint8_t *data,
                           size_t capacity, size_t *count)
{
    fw_status_t result;
    if (count == NULL) return FW_EINVAL;
    *count = 0U;
    if (device == NULL || device->ops == NULL || device->ops->read == NULL ||
        (capacity != 0U && data == NULL)) return FW_EINVAL;
    if (capacity == 0U) return FW_OK;
    result = device->ops->read(device->ctx, data, capacity, count);
    /* 端口声称超过 capacity 时已经破坏了内存；拒绝该值，
     * 避免调用者按它计算重试长度。 */
    if (*count > capacity) {
        *count = 0U;
        return FW_EIO;
    }
    return result;
}

fw_status_t fw_stream_write(const fw_stream_t *device, const uint8_t *data,
                            size_t length, size_t *count, uint32_t timeout_ms)
{
    fw_status_t result;
    if (count == NULL) return FW_EINVAL;
    *count = 0U;
    if (device == NULL || device->ops == NULL || device->ops->write == NULL ||
        (length != 0U && data == NULL)) return FW_EINVAL;
    if (length == 0U) return FW_OK;
    result = device->ops->write(device->ctx, data, length, count, timeout_ms);
    if (*count > length) {
        *count = 0U;
        return FW_EIO;
    }
    return result;
}

/* -------------------------------------------------------------------------
 * 时间辅助
 * ------------------------------------------------------------------------- */
uint32_t fw_elapsed_ms(uint32_t now, uint32_t then)
{
    /* 无符号回绕是有意设计：契约限制时间差小于 2^31 ms，因此直接相减正确。 */
    return now - then;
}
