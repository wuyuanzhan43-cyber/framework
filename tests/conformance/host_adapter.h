/*
 * host_adapter.h - 把 ports/host 接到一致性套件。
 *
 * 层级：test
 * 职责：feed/drain/fault hook 的示例实现。
 * 上下文：仅测试线程。
 * 依赖：fw/conformance.h、fw/host.h。
 */
#ifndef FW_CONFORMANCE_HOST_ADAPTER_H
#define FW_CONFORMANCE_HOST_ADAPTER_H

#include "fw/conformance.h"
#include "fw/host.h"

/* 示例：把端口接入一致性套件。
 *
 * 这是端口作者可以照抄的文件。每个 hook 只有几行，并映射到端口已有的
 * 测试旋钮（或为此新增的旋钮）。没有故障注入的端口可以留空这些 hook，
 * 依赖它们的条款会得到 SKIP。 */

/* 把 ports/host 接到 stream 套件。host 同时充当 ops 上下文和 harness
 * 上下文，因此一次调用覆盖两者。 */
fw_conformance_stream_io_t host_conformance_stream_io(fw_host_t *host);
fw_conformance_indicator_io_t host_conformance_indicator_io(fw_host_t *host);

#endif
