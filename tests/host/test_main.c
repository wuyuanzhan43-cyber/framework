/*
 * tests/host/test_main.c - Host 侧行为与契约测试。
 *
 * 层级：test/host
 * 职责：面向内存 Host 端口，验证应用调度、协议组帧、ring 和故障恢复。
 * 上下文：单线程；每个测试组使用独立夹具。
 * 依赖：fw/demo_app.h、fw/host.h、stdio.h、stdlib.h、string.h。
 *
 * 运行：ctest --preset host-debug
 */
#include "fw/demo_app.h"
#include "fw/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks; /* 整个测试二进制执行的断言数 */
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

/* 新建一对 host + app，时间从 0 开始，没有待处理工作。 */
static void fixture(fw_host_t *host, fw_demo_app_t *app)
{
    fw_host_init(host);
    CHECK(fw_demo_init(app, fw_host_stream(host), fw_host_indicator(host),
                       fw_host_clock(host)) == FW_OK);
}

/* 用真实编码器生成一个合法请求帧。 */
static size_t encode(uint8_t cmd, uint8_t seq, const uint8_t *data, size_t length,
                      uint8_t *frame)
{
    fw_message_t message = {0};
    size_t written;
    CHECK(length <= FW_PROTOCOL_MAX_PAYLOAD);
    message.command = cmd;
    message.sequence = seq;
    message.length = (uint8_t)length;
    if (length != 0U) memcpy(message.payload, data, length);
    CHECK(fw_protocol_encode(&message, frame, FW_PROTOCOL_MAX_FRAME, &written) == FW_OK);
    return written;
}

/* 把一个编码后的请求放入 host RX ring。 */
static void request(fw_host_t *host, uint8_t cmd, uint8_t seq,
                     const uint8_t *data, size_t length)
{
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t written = encode(cmd, seq, data, length, frame);
    CHECK(fw_host_receive(host, frame, written) == written);
}

/* 从 host TX ring 中解析恰好一条合法回复。 */
static fw_message_t response(fw_host_t *host)
{
    fw_parser_t parser;
    fw_message_t message = {0};
    uint8_t byte;
    unsigned messages = 0U;
    fw_parser_init(&parser);
    while (fw_host_take_tx(host, &byte, 1U) != 0U) {
        if (fw_parser_push(&parser, byte, &message)) ++messages;
    }
    CHECK(messages == 1U);
    CHECK(!fw_parser_partial(&parser));
    CHECK(parser.bad_crc == 0U);
    return message;
}

/* ring 环绕/满/空、容量=1、peek 不消费以及参照 FIFO。 */
static void test_ring(void)
{
    fw_ring_t ring;
    uint8_t storage[7], reference[7], data[7];
    size_t used = 0U;
    uint32_t random = 123U;
    unsigned step;
    CHECK(fw_ring_init(NULL, storage, sizeof(storage)) == FW_EINVAL);
    CHECK(fw_ring_init(&ring, storage, 0U) == FW_EINVAL);
    CHECK(fw_ring_init(&ring, storage, sizeof(storage)) == FW_OK);
    for (step = 0U; step < 2000U; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        if ((random & 0x100U) != 0U) {
            uint8_t byte = (uint8_t)(random >> 16U);
            size_t actual = fw_ring_write(&ring, &byte, 1U);
            CHECK(actual == (used < sizeof(reference) ? 1U : 0U));
            if (actual != 0U) reference[used++] = byte;
        } else {
            size_t actual = fw_ring_read(&ring, data, 1U);
            CHECK(actual == (used != 0U ? 1U : 0U));
            if (actual != 0U) {
                CHECK(data[0] == reference[0]);
                --used;
                memmove(reference, reference + 1, used);
            }
        }
        CHECK(ring.count == used);
    }
    fw_ring_clear(&ring);
    CHECK(ring.count == 0U);
    CHECK(fw_ring_init(&ring, storage, 1U) == FW_OK);
    CHECK(fw_ring_write(&ring, (const uint8_t *)"AB", 2U) == 1U);
    CHECK(fw_ring_read(&ring, data, sizeof(data)) == 1U && data[0] == 'A');
    CHECK(fw_ring_init(&ring, storage, sizeof(storage)) == FW_OK);
    CHECK(fw_ring_write(&ring, (const uint8_t *)"ABCDE", 5U) == 5U);
    CHECK(fw_ring_peek(&ring, data, 3U) == 3U && memcmp(data, "ABC", 3U) == 0);
    CHECK(ring.count == 5U); /* peek 不得消费数据。 */
    CHECK(fw_ring_peek(&ring, data, sizeof(data)) == 5U &&
          memcmp(data, "ABCDE", 5U) == 0);
    CHECK(fw_ring_read(&ring, data, sizeof(data)) == 5U &&
          memcmp(data, "ABCDE", 5U) == 0);
    puts("PASS ring: wrap, full, empty, capacity=1, peek, reference FIFO");
}

/* CRC 向量、分片、最大 payload、损坏和噪声重同步。 */
static void test_protocol(void)
{
    fw_parser_t parser;
    fw_message_t message = {0};
    uint8_t frame[FW_PROTOCOL_MAX_FRAME], payload[FW_PROTOCOL_MAX_PAYLOAD];
    size_t length, i, n;
    unsigned decoded = 0U;
    uint32_t random = 42U;
    CHECK(fw_crc16((const uint8_t *)"123456789", 9U) == 0x29b1U);
    fw_parser_init(&parser);
    memset(payload, 0xa5, sizeof(payload));
    length = encode(FW_CMD_LED_SET, 17U, payload, sizeof(payload), frame);
    for (i = 0U; i < length; ++i) {
        bool ready = fw_parser_push(&parser, frame[i], &message);
        CHECK(ready == (i + 1U == length));
    }
    CHECK(message.sequence == 17U && message.length == sizeof(payload));
    CHECK(memcmp(message.payload, payload, sizeof(payload)) == 0);
    CHECK(fw_protocol_encode(&message, frame, 1U, &n) == FW_EOVERFLOW && n == 0U);
    frame[length - 1U] ^= 1U;
    for (i = 0U; i < length; ++i) CHECK(!fw_parser_push(&parser, frame[i], &message));
    CHECK(parser.bad_crc == 1U);
    fw_parser_push(&parser, 0xa5U, &message);
    fw_parser_push(&parser, 0xa5U, &message); /* 重叠前导码。 */
    fw_parser_push(&parser, 0x5aU, &message);
    fw_parser_push(&parser, 0xffU, &message);
    CHECK(parser.bad_length == 1U && !fw_parser_partial(&parser));
    length = encode(FW_CMD_PING, 3U, NULL, 0U, frame);
    for (n = 0U; n < 2U; ++n)
        for (i = 0U; i < length; ++i)
            if (fw_parser_push(&parser, frame[i], &message)) ++decoded;
    CHECK(decoded == 2U);
    for (i = 0U; i < 20000U; ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        (void)fw_parser_push(&parser, (uint8_t)(random >> 24U), &message);
        CHECK(parser.used <= sizeof(parser.body));
    }
    for (i = 0U; i < 64U; ++i) (void)fw_parser_push(&parser, 0U, &message);
    decoded = 0U;
    for (i = 0U; i < length; ++i)
        if (fw_parser_push(&parser, frame[i], &message)) ++decoded;
    CHECK(decoded == 1U && message.sequence == 3U);
    puts("PASS protocol: known CRC, fragmentation, max payload, corruption, noise recovery");
}

/* PING/STATUS/LED_SET、错误输入、设备故障、隔离、可选输出。 */
static void test_business(void)
{
    fw_host_t host, other;
    fw_demo_app_t app;
    fw_message_t reply;
    uint8_t on = 1U, bad = 2U, off = 0U;
    fixture(&host, &app);
    fw_host_init(&other);
    request(&host, FW_CMD_STATUS, 1U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.command == 0x83U && reply.payload[1] == 0U);
    request(&host, FW_CMD_LED_SET, 2U, &on, 1U);
    CHECK(fw_demo_poll(&app) == FW_OK && host.led_on && !other.led_on);
    reply = response(&host);
    CHECK(reply.sequence == 2U && reply.payload[0] == 0U && host.led_writes == 1U);
    request(&host, FW_CMD_LED_SET, 3U, &bad, 1U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.payload[0] == 1U && host.led_writes == 1U);
    host.indicator_fault = FW_EIO;
    request(&host, FW_CMD_LED_SET, 4U, &off, 1U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.payload[0] == 3U && app.led_requested_on && host.led_writes == 1U);
    request(&host, 0x7fU, 5U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.payload[0] == 2U);
    request(&host, 0x81U, 6U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U);
    request(&host, FW_CMD_PING, 7U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.length == 5U && memcmp(&reply.payload[1], "PONG", 4U) == 0);
    /* 表驱动 STATS 让运行时计数器无需调试器即可观察。 */
    request(&host, FW_CMD_STATS, 70U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.command == 0x84U && reply.length == 14U && reply.payload[0] == 0U);
    CHECK(reply.payload[1] == 8U); /* STATUS、三次 LED、未知命令、回复、PING、STATS。 */
    CHECK(reply.payload[5] == 0U && reply.payload[9] == 0U && reply.payload[13] == 0U);
    /* 没有输出设备的板子是合法配置。 */
    {
        fw_host_t bare_host;
        fw_demo_app_t bare_app;
        fw_indicator_t none = {NULL, NULL};
        fw_host_init(&bare_host);
        CHECK(fw_demo_init(&bare_app, fw_host_stream(&bare_host), none,
                           fw_host_clock(&bare_host)) == FW_OK);
        request(&bare_host, FW_CMD_LED_SET, 8U, &on, 1U);
        CHECK(fw_demo_poll(&bare_app) == FW_OK);
        reply = response(&bare_host);
        CHECK(reply.payload[0] == 3U && bare_host.led_writes == 0U);
    }
    puts("PASS business: ping, status, LED, bad input, device failure, instance isolation, optional output");
}

/* 短写保留后缀；停滞的 TX 不会重复前缀。 */
static void test_backpressure(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t reply;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME], on = 1U;
    size_t expected;
    unsigned i;
    fixture(&host, &app);
    host.write_limit = 2U;
    host.write_fault = FW_ETIMEOUT;
    request(&host, FW_CMD_PING, 8U, NULL, 0U);
    request(&host, FW_CMD_LED_SET, 9U, &on, 1U);
    CHECK(fw_demo_poll(&app) == FW_ETIMEOUT);
    CHECK(app.tx_offset == 2U && app.frames == 1U && host.led_writes == 0U);
    for (i = 0U; i < 20U && app.tx_length != 0U; ++i) (void)fw_demo_poll(&app);
    CHECK(app.tx_length == 0U && app.frames == 1U);
    reply = response(&host);
    CHECK(reply.sequence == 8U && reply.length == 5U); /* 不重复前缀。 */
    (void)fw_demo_poll(&app);
    for (i = 0U; i < 20U && app.tx_length != 0U; ++i) (void)fw_demo_poll(&app);
    CHECK(host.led_writes == 1U && app.frames == 2U);
    reply = response(&host);
    CHECK(reply.sequence == 9U);
    host.write_limit = 0U;
    request(&host, FW_CMD_PING, 10U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_EAGAIN);
    expected = app.tx_length;
    CHECK(expected != 0U && app.tx_offset == 0U);
    CHECK(fw_demo_poll(&app) == FW_EAGAIN && app.tx_length == expected);
    host.write_limit = sizeof(frame);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.sequence == 10U);
    puts("PASS transport: partial timeout, retained suffix, bounded backpressure");
}

/* tick 回绕、半帧过期、RX 溢出以及之后的新请求。 */
static void test_recovery(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t reply;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME], fill[FW_HOST_BUFFER_SIZE + 1U];
    size_t size;
    fixture(&host, &app);
    host.now_ms = UINT32_MAX - 10U;
    size = encode(FW_CMD_PING, 11U, NULL, 0U, frame);
    CHECK(fw_host_receive(&host, frame, 3U) == 3U);
    CHECK(fw_demo_poll(&app) == FW_OK && fw_parser_partial(&app.parser));
    host.now_ms += 250U;
    CHECK(fw_demo_poll(&app) == FW_OK && app.partial_timeouts == 1U);
    CHECK(fw_host_receive(&host, frame, size) == size);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.sequence == 11U);
    memset(fill, 0x11, sizeof(fill));
    CHECK(fw_host_receive(&host, fill, sizeof(fill)) == FW_HOST_BUFFER_SIZE);
    CHECK(fw_demo_poll(&app) == FW_EOVERFLOW && app.io_errors == 1U);
    request(&host, FW_CMD_PING, 12U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.sequence == 12U);
    CHECK(fw_elapsed_ms(5U, UINT32_MAX - 4U) == 10U);
    puts("PASS recovery: tick wrap, partial expiry, RX overflow, fresh request");
}

/* 回归：半帧超时到期时，端口 ring 中可能已有一帧完整数据。
 * 超时不能丢弃它。 */
/* 回归：端口已缓冲的完整帧不应因过期而丢失。 */
static void test_partial_timeout_keeps_buffered_frame(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t reply;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t size;
    fixture(&host, &app);
    size = encode(FW_CMD_PING, 31U, NULL, 0U, frame);
    CHECK(fw_host_receive(&host, frame, 3U) == 3U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    CHECK(fw_parser_partial(&app.parser));
    CHECK(fw_host_receive(&host, frame + 3U, size - 3U) == size - 3U);
    host.now_ms += FW_DEMO_PARTIAL_TIMEOUT_MS + 50U;
    CHECK(fw_demo_poll(&app) == FW_OK);
    reply = response(&host);
    CHECK(reply.command == 0x81U && reply.sequence == 31U);
    CHECK(app.frames == 1U && app.partial_timeouts == 0U);
    puts("PASS partial timeout: a complete frame buffered in the port is not discarded");
}

/* 回归：1 ms 上报周期曾导致在 RX 循环前返回，
 * 持续上报会永久饿死请求。 */
/* 回归：1 ms 上报周期不得饿死 RX 处理。 */
static void test_small_report_period_keeps_rx_alive(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t size;
    unsigned i;
    fixture(&host, &app);
    CHECK(fw_demo_set_report_period(&app, 1U) == FW_OK);
    size = encode(FW_CMD_PING, 41U, NULL, 0U, frame);
    CHECK(fw_host_receive(&host, frame, size) == size);
    CHECK(fw_host_receive(&host, frame, size) == size);
    for (i = 0U; i < 10U; ++i) {
        host.now_ms += 1U;
        CHECK(fw_demo_poll(&app) == FW_OK);
    }
    CHECK(app.frames == 2U);
    CHECK(app.report_sequence != 0U);
    CHECK(app.partial_timeouts == 0U && host.rx.count == 0U);
    puts("PASS report fairness: 1 ms reports and RX both make progress");
}

/* 运行时限制：自定义过期、超时复位和恢复。 */
static void test_configurable_limits(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t size;
    fixture(&host, &app);
    CHECK(fw_demo_set_limits(&app, 100U, 20U, 64U) == FW_OK);
    size = encode(FW_CMD_PING, 51U, NULL, 0U, frame);
    CHECK(fw_host_receive(&host, frame, 3U) == 3U);
    CHECK(fw_demo_poll(&app) == FW_OK && fw_parser_partial(&app.parser));
    host.now_ms += 99U;
    CHECK(fw_demo_poll(&app) == FW_OK && fw_parser_partial(&app.parser));
    host.now_ms += 1U;
    CHECK(fw_demo_poll(&app) == FW_OK && !fw_parser_partial(&app.parser));
    CHECK(app.partial_timeouts == 1U);
    /* 过期后新帧仍能正常工作。 */
    CHECK(fw_host_receive(&host, frame, size) == size);
    CHECK(fw_demo_poll(&app) == FW_OK);
    (void)response(&host);
    CHECK(app.frames == 1U);
    puts("PASS configurable limits: partial timeout is honoured and recovery works");
}

/* 事件循环等待预算：上报、半帧超时、TX 挂起和空闲。 */
static void test_next_wait(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t size;
    fixture(&host, &app);
    host.now_ms = 5000U;
    CHECK(fw_demo_next_wait_ms(NULL) == 0U);
    CHECK(fw_demo_next_wait_ms(&app) == FW_DEMO_WAIT_FOREVER);
    CHECK(fw_demo_set_report_period(&app, 1000U) == FW_OK);
    CHECK(fw_demo_next_wait_ms(&app) == 1000U);
    host.now_ms = 5500U;
    CHECK(fw_demo_next_wait_ms(&app) == 500U);
    host.now_ms = 6000U;
    CHECK(fw_demo_next_wait_ms(&app) == 0U);
    size = encode(FW_CMD_PING, 61U, NULL, 0U, frame);
    CHECK(fw_host_receive(&host, frame, 3U) == 3U);
    CHECK(fw_demo_poll(&app) == FW_OK && fw_parser_partial(&app.parser));
    /* 上报刚触发；半帧期限现在是最近的。 */
    CHECK(fw_demo_next_wait_ms(&app) == FW_DEMO_PARTIAL_TIMEOUT_MS);
    host.now_ms += 100U;
    CHECK(fw_demo_next_wait_ms(&app) == FW_DEMO_PARTIAL_TIMEOUT_MS - 100U);
    host.write_limit = 0U;
    CHECK(fw_host_receive(&host, frame + 3U, size - 3U) == size - 3U);
    CHECK(fw_demo_poll(&app) == FW_EAGAIN);
    CHECK(fw_demo_next_wait_ms(&app) == 1U); /* TX 已排队；完成中断可以唤醒我们。 */
    puts("PASS next wait: report, partial timeout, TX pending, idle");
}

/* 上报选择加入、周期边界、状态、合并、停用和回绕。 */
static void test_periodic_report(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t message;
    uint8_t on = 1U;
    fixture(&host, &app);
    host.now_ms = 5000U;
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U); /* 选择加入。 */
    CHECK(fw_demo_set_report_period(NULL, 1000U) == FW_EINVAL);
    CHECK(fw_demo_set_report_period(&app, UINT32_MAX) == FW_EINVAL);
    CHECK(fw_demo_set_report_period(&app, 1000U) == FW_OK);
    host.now_ms += 999U;
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U);
    ++host.now_ms;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.command == FW_EVENT_STATUS && message.sequence == 0U);
    CHECK(message.length == 3U && message.payload[0] == 0U && message.payload[1] == 0U);
    CHECK(host.led_writes == 0U); /* 上报绝不能改变 LED。 */
    request(&host, FW_CMD_LED_SET, 42U, &on, 1U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.command == 0x82U && message.sequence == 42U);
    host.now_ms += 1000U;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.command == FW_EVENT_STATUS && message.sequence == 1U);
    CHECK(message.payload[1] == 1U && message.payload[2] == 1U && host.led_writes == 1U);
    host.now_ms += 10000U;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.sequence == 2U);
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U); /* 不补发突发。 */
    CHECK(fw_demo_set_report_period(&app, 0U) == FW_OK);
    host.now_ms += 1000U;
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U);
    host.now_ms = UINT32_MAX - 500U;
    CHECK(fw_demo_set_report_period(&app, 1000U) == FW_OK);
    app.report_sequence = 255U;
    host.now_ms += 1000U;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.sequence == 255U);
    host.now_ms += 1000U;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.sequence == 0U);
    puts("PASS periodic report: opt-in, timing, state, coalescing, disable, tick/sequence wrap");
}

/* 回复和上报绝不交错或互相覆盖。 */
static void test_report_serialization(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t message;
    unsigned i;
    fixture(&host, &app);
    CHECK(fw_demo_set_report_period(&app, 1000U) == FW_OK);
    host.write_limit = 2U;
    host.write_fault = FW_ETIMEOUT;
    request(&host, FW_CMD_PING, 23U, NULL, 0U);
    CHECK(fw_demo_poll(&app) == FW_ETIMEOUT && app.tx_offset == 2U);
    host.now_ms = 1000U; /* 回复未完成时上报已到期。 */
    for (i = 0U; i < 20U && app.tx_length != 0U; ++i) (void)fw_demo_poll(&app);
    CHECK(app.tx_length == 0U && app.report_sequence == 0U);
    message = response(&host);
    CHECK(message.command == 0x81U && message.sequence == 23U);
    /* RX 先获得有界机会：先回复请求，
     * 回复排空后再发送上报。 */
    request(&host, FW_CMD_PING, 24U, NULL, 0U);
    host.write_limit = 0U;
    CHECK(fw_demo_poll(&app) == FW_EAGAIN);
    CHECK(app.tx_offset == 0U && app.report_sequence == 0U && app.frames == 2U);
    host.now_ms += 10000U;
    CHECK(fw_demo_poll(&app) == FW_EAGAIN && app.report_sequence == 0U);
    host.write_limit = FW_PROTOCOL_MAX_FRAME;
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.command == 0x81U && message.sequence == 24U);
    CHECK(fw_demo_poll(&app) == FW_OK);
    message = response(&host);
    CHECK(message.command == FW_EVENT_STATUS && message.sequence == 0U);
    /* 停用只停止后续上报，不取消已排队帧。 */
    CHECK(fw_demo_set_report_period(&app, 1000U) == FW_OK);
    host.now_ms += 1000U;
    host.write_limit = 0U;
    CHECK(fw_demo_poll(&app) == FW_EAGAIN);
    CHECK(app.report_sequence == 2U && app.tx_offset == 0U);
    CHECK(fw_demo_set_report_period(&app, 0U) == FW_OK);
    host.write_limit = 2U;
    host.write_fault = FW_ETIMEOUT; /* 故障注入只作用于一次 write。 */
    CHECK(fw_demo_poll(&app) == FW_ETIMEOUT && app.tx_offset == 2U);
    for (i = 0U; i < 20U && app.tx_length != 0U; ++i) (void)fw_demo_poll(&app);
    CHECK(app.tx_length == 0U && app.frames == 2U);
    message = response(&host);
    CHECK(message.command == FW_EVENT_STATUS && message.sequence == 1U);
    host.write_limit = FW_PROTOCOL_MAX_FRAME;
    host.write_fault = FW_OK;
    CHECK(fw_demo_poll(&app) == FW_OK && host.tx.count == 0U);
    puts("PASS report serialization: reply/report order, stalled TX, no interleaving");
}

/* 公共 API 的 NULL/零长度/非法依赖防护。 */
static void test_contract_guards(void)
{
    fw_host_t host;
    fw_host_t limits_host;
    fw_stream_t stream;
    fw_demo_app_t app;
    fw_demo_app_t limits_app;
    fw_ring_t ring;
    fw_message_t message = {0};
    size_t count = 123U;
    fw_host_init(&host);
    fw_host_init(&limits_host);
    stream = fw_host_stream(&host);
    CHECK(fw_stream_read(NULL, NULL, 0U, &count) == FW_EINVAL && count == 0U);
    CHECK(fw_stream_read(&stream, NULL, 1U, &count) == FW_EINVAL);
    CHECK(fw_stream_read(&stream, NULL, 0U, &count) == FW_OK && count == 0U);
    CHECK(fw_stream_write(&stream, NULL, 0U, &count, 0U) == FW_OK);
    CHECK(fw_stream_write(&stream, NULL, 1U, &count, 0U) == FW_EINVAL);
    CHECK(fw_indicator_set(NULL, true) == FW_EINVAL);
    CHECK(fw_demo_poll(NULL) == FW_EINVAL);
    CHECK(fw_ring_init(&ring, NULL, 4U) == FW_EINVAL);
    CHECK(fw_protocol_encode(&message, NULL, FW_PROTOCOL_MAX_FRAME, &count) == FW_EINVAL);
    CHECK(count == 0U);
    stream.ops = NULL;
    CHECK(fw_demo_init(&app, stream, fw_host_indicator(&host), fw_host_clock(&host)) == FW_EINVAL);
    CHECK(fw_demo_init(&limits_app, fw_host_stream(&limits_host),
                       fw_host_indicator(&limits_host), fw_host_clock(&limits_host)) == FW_OK);
    CHECK(fw_demo_set_limits(NULL, 250U, 20U, 64U) == FW_EINVAL);
    CHECK(fw_demo_set_limits(&limits_app, UINT32_MAX, 20U, 64U) == FW_EINVAL);
    CHECK(fw_demo_set_limits(&limits_app, 250U, 20U, 0U) == FW_EINVAL);
    CHECK(fw_demo_set_limits(&limits_app, 250U, 20U, FW_DEMO_MAX_RX_BUDGET + 1U) == FW_EINVAL);
    CHECK(fw_demo_set_limits(&limits_app, 0U, 20U, 64U) == FW_OK); /* 0 表示关闭过期。 */
    puts("PASS contracts: null, zero length, invalid dependencies, invalid limits");
}

/* 一个违反 io.h、多报 accepted 的后端。调用者按 `count` 计算重试，
 * 因此包装层必须拒绝该值，而不是继续传递；否则会变成调用者缓冲区越界。 */
static fw_status_t greedy_read(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    (void)ctx;
    (void)data;
    *count = capacity + 1U;
    return FW_OK;
}

static fw_status_t greedy_write(void *ctx, const uint8_t *data, size_t length,
                                size_t *count, uint32_t timeout_ms)
{
    (void)ctx;
    (void)data;
    (void)timeout_ms;
    *count = length + 1U;
    return FW_OK;
}

/* 多报进展的后端会被 io 包装层拒绝。 */
static void test_wrapper_guards(void)
{
    static const fw_stream_ops_t greedy_ops = { greedy_read, greedy_write };
    fw_stream_t greedy = { &greedy_ops, NULL };
    uint8_t buffer[4] = {0};
    size_t count = 99U;
    CHECK(fw_stream_read(&greedy, buffer, sizeof(buffer), &count) == FW_EIO);
    CHECK(count == 0U);
    CHECK(fw_stream_write(&greedy, buffer, sizeof(buffer), &count, 0U) == FW_EIO);
    CHECK(count == 0U);
    puts("PASS wrapper guards: over-reported count from a backend is rejected");
}

/* 运行所有组；本二进制中的 CHECK() 在首次失败时退出。 */
int main(void)
{
    test_ring();
    test_protocol();
    test_business();
    test_backpressure();
    test_recovery();
    test_partial_timeout_keeps_buffered_frame();
    test_small_report_period_keeps_rx_alive();
    test_configurable_limits();
    test_next_wait();
    test_periodic_report();
    test_report_serialization();
    test_contract_guards();
    test_wrapper_guards();
    printf("ALL PASSED (%u checks)\n", checks);
    return 0;
}
