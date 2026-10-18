/*
 * fw/status.h - 框架统一状态码。
 *
 * 层级：framework/core
 * 职责：为 core、components、ports 和 tests 提供同一套错误语义。
 * 上下文：纯类型定义，不涉及共享状态。
 * 依赖：无。
 */
#ifndef FW_STATUS_H
#define FW_STATUS_H

typedef enum {
    FW_OK = 0,          /* 操作按要求完成 */
    FW_EINVAL = -1,     /* 参数、配置或调用顺序非法 */
    FW_EAGAIN = -2,     /* 暂时性背压；稍后重试同一工作 */
    FW_EIO = -3,        /* 设备/链路错误；连续性可能已丢失 */
    FW_ETIMEOUT = -4,   /* 操作未在期限内完成 */
    FW_EOVERFLOW = -5,  /* 有界缓冲/流溢出；已有数据被丢弃 */
    FW_ENOTREADY = -6,  /* 设备存在但尚未初始化/启动 */
    FW_ENOTSUP = -7     /* 当前实现不支持该操作 */
} fw_status_t;

/* 把状态码转成稳定的可读字符串，供日志和调试器使用；不会返回 NULL。 */
const char *fw_status_name(fw_status_t status);

#endif
