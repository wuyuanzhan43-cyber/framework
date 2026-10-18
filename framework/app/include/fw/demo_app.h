/*
 * fw/demo_app.h - 示例应用状态与公开 API。
 *
 * 层级：app（示例，不属于框架核心）
 * 职责：命令分发、LED 状态、周期上报和轮询调度。
 * 上下文：单拥有任务；同一实例上的 fw_demo_poll() 不允许重入。
 * 依赖：fw/io.h、fw/protocol.h。
 */
#ifndef FW_DEMO_APP_H
#define FW_DEMO_APP_H

#include "fw/io.h"
#include "fw/protocol.h"

/* 请求命令 bit7=0；回复命令为 command|0x80。 */
#define FW_CMD_PING 0x01U
#define FW_CMD_LED_SET 0x02U
#define FW_CMD_STATUS 0x03U
#define FW_CMD_STATS 0x04U

/* 主动状态帧；使用独立序号空间。 */
#define FW_EVENT_STATUS 0x90U
#define FW_DEMO_REPORT_PERIOD_MS 1000U

/* 默认策略，可在编译期覆盖，也可在运行时通过 fw_demo_set_limits() 调整。
 * 半帧超时必须大于一帧的最坏传输时间：
 * FW_PROTOCOL_MAX_FRAME 字节 × 10 bit/字节 ÷ 波特率。115200 下约 3.4 ms；
 * 1200 baud 下一帧约 325 ms，建议至少 400 ms。设为 0 可关闭超时。 */
#ifndef FW_DEMO_RX_BUDGET
#define FW_DEMO_RX_BUDGET 64U
#endif
#ifndef FW_DEMO_TX_TIMEOUT_MS
#define FW_DEMO_TX_TIMEOUT_MS 20U
#endif
#ifndef FW_DEMO_PARTIAL_TIMEOUT_MS
#define FW_DEMO_PARTIAL_TIMEOUT_MS 250U
#endif
#define FW_DEMO_MAX_RX_BUDGET 4096U

#if (FW_DEMO_RX_BUDGET < 1) || (FW_DEMO_RX_BUDGET > FW_DEMO_MAX_RX_BUDGET)
#error "FW_DEMO_RX_BUDGET 必须在 1..FW_DEMO_MAX_RX_BUDGET 之间"
#endif
#if (FW_DEMO_PARTIAL_TIMEOUT_MS < 0) || (FW_DEMO_PARTIAL_TIMEOUT_MS > INT32_MAX)
#error "FW_DEMO_PARTIAL_TIMEOUT_MS 必须在 0..INT32_MAX 之间"
#endif

/* 请求命令 bit7=0；回复为 command|0x80，SEQ 沿用请求值。
 * payload[0] 状态码：0 成功、1 参数错误、2 不支持、3 输出不可用。
 * PING(空)->status+'PONG'；LED_SET(0/1)->status；
 * STATUS(空)->status、known(0/1)、最后一次成功 LED 请求(0/1)；
 * STATS(空)->status、frames(u32 LE)、io_errors(u32)、
 * partial_timeouts(u32)、report_sequence(u8)。
 * SEQ 仅用于关联；命令幂等，不做去重。 */
typedef struct {
    fw_stream_t stream;       /* 借用的设备；生命周期必须覆盖本结构 */
    fw_indicator_t indicator; /* 可选；ops==NULL 表示没有输出设备 */
    fw_clock_t clock;         /* 借用的单调毫秒时钟 */
    fw_parser_t parser;       /* RX 帧状态；由轮询任务拥有 */

    uint8_t tx[FW_PROTOCOL_MAX_FRAME];  /* 当前在途的一帧回复/上报 */
    size_t tx_length;                   /* 当前待发送的编码字节数 */
    size_t tx_offset;                   /* 已交给端口的前缀长度 */

    uint32_t last_rx_ms;       /* 最近一次消费 RX 字节的 poll 起始时间 */
    uint32_t frames;           /* 已分发的合法帧数，包含被忽略的回复 */
    /* 统计除 EAGAIN 外的读写错误，不代表丢帧数。
     * 即使所有字节都被接受，完成超时也会计入。 */
    uint32_t io_errors;
    uint32_t partial_timeouts; /* 空闲半帧被丢弃的次数 */

    uint32_t report_period_ms; /* 0 表示关闭主动上报 */
    uint32_t last_report_ms;   /* 调度基准；错过的周期会合并 */
    uint32_t partial_timeout_ms; /* 0 表示半帧永不过期 */
    uint32_t tx_timeout_ms;      /* 传给每次 stream write */
    uint32_t rx_budget;          /* 每轮最多消费的字节数 */

    uint8_t report_sequence;   /* 独立于请求 SEQ；按 256 回绕 */
    bool led_known;            /* 至少有一次 LED_SET 成功 */
    bool led_requested_on;     /* 最后一次成功请求的逻辑状态，非实测 */
} fw_demo_app_t;

/* stream 和 clock 必填。indicator.ops 可以为 NULL，表示板上没有输出设备；
 * 此时 LED_SET 回复状态 3（输出不可用）。 */
fw_status_t fw_demo_init(fw_demo_app_t *app, fw_stream_t stream,
                         fw_indicator_t indicator, fw_clock_t clock);

/* 调整每轮限制。partial_timeout_ms=0 关闭半帧过期。
 * rx_budget 限制每轮消费字节数（1..FW_DEMO_MAX_RX_BUDGET）。
 * tx_timeout_ms 传给每次 stream write。只允许在拥有 poll 的任务中调用；
 * 同一实例上不得与 poll 并发。 */
fw_status_t fw_demo_set_limits(fw_demo_app_t *app, uint32_t partial_timeout_ms,
                               uint32_t tx_timeout_ms, uint32_t rx_budget);

/* 可选主动上报：CMD=0x90，payload=status(0)、known、state。
 * 首次在 period_ms 后发送；0 停止后续上报，但不清除已在途的 TX。
 * 序号从 0 开始，按 256 回绕；错过的周期合并。
 * 合法周期：0..INT32_MAX ms。必须在拥有 poll 的同一任务中调用。 */
fw_status_t fw_demo_set_report_period(fw_demo_app_t *app, uint32_t period_ms);

/* 返回下一次需要 poll 的等待毫秒数：
 *   - TX 未完成：1 ms（完成回调也会唤醒任务）；
 *   - 上报或半帧超时已到期：0；
 *   - 否则返回最近的剩余期限；
 *   - 没有期限：FW_DEMO_WAIT_FOREVER（由设备事件唤醒）。
 * 使用应用时钟；只能在拥有任务中调用。 */
#define FW_DEMO_WAIT_FOREVER UINT32_MAX
uint32_t fw_demo_next_wait_ms(const fw_demo_app_t *app);

/* 单拥有任务的一轮有界调度。
 *
 * 顺序很重要：先排空 TX，再有界消费 RX，仅在本轮没有读到任何字节时让
 * 半帧超时，最后发送到期上报。这个顺序同时避免“上报周期饿死 RX”和
 * “超时误丢端口 ring 中已经完整的帧”。
 * TX 背压会延迟 RX；禁止在 ISR 中调用。 */
fw_status_t fw_demo_poll(fw_demo_app_t *app);

#endif
