/*
 * fw/demo_app.c - 示例命令分发、TX 泵和轮询调度。
 *
 * 层级：app（示例）
 * 职责：把解析后的协议帧转为指示器动作、回复和主动上报。
 * 上下文：轮询任务拥有全部字段；只有在 fw_stream_write() 报告 accepted
 *         之后，端口才拥有 TX 缓冲区。
 * 依赖：fw/demo_app.h、string.h。
 */
#include "fw/demo_app.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * 私有类型
 * ------------------------------------------------------------------------- */
typedef void (*fw_demo_handler_t)(fw_demo_app_t *app,
                                  const fw_message_t *request,
                                  fw_message_t *reply);

typedef struct {
    uint8_t command;          /* 请求命令码（bit7=0） */
    fw_demo_handler_t handler; /* 填写回复；状态码初始为 payload[0]=0 */
} fw_demo_command_t;

/* -------------------------------------------------------------------------
 * 命令处理函数
 * ------------------------------------------------------------------------- */
/* 多字节计数器在线路上统一使用小端序。 */
static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
    out[2] = (uint8_t)(value >> 16U);
    out[3] = (uint8_t)(value >> 24U);
}

/* PING：空请求 -> 状态 0 + "PONG"；带 payload 视为参数错误。 */
static void cmd_ping(fw_demo_app_t *app, const fw_message_t *request,
                     fw_message_t *reply)
{
    (void)app;
    if (request->length != 0U) {
        reply->payload[0] = 1U;
        return;
    }
    reply->length = 5U;
    memcpy(&reply->payload[1], "PONG", 4U);
}

/* LED_SET：恰好 1 字节 payload，取值 0/1。设备失败时保留之前已知的
 * 应用状态，并回复状态 3。 */
static void cmd_led_set(fw_demo_app_t *app, const fw_message_t *request,
                        fw_message_t *reply)
{
    fw_status_t result;
    if (request->length != 1U || request->payload[0] > 1U) {
        reply->payload[0] = 1U;
        return;
    }
    result = fw_indicator_set(&app->indicator, request->payload[0] != 0U);
    if (result == FW_OK) {
        app->led_known = true;
        app->led_requested_on = request->payload[0] != 0U;
    } else {
        reply->payload[0] = 3U;
    }
}

/* STATUS：报告软件记录的状态，不是实测 LED 电流或亮度。 */
static void cmd_status(fw_demo_app_t *app, const fw_message_t *request,
                       fw_message_t *reply)
{
    if (request->length != 0U) {
        reply->payload[0] = 1U;
        return;
    }
    reply->length = 3U;
    reply->payload[1] = app->led_known ? 1U : 0U;
    reply->payload[2] = app->led_requested_on ? 1U : 0U;
}

/* STATS：状态字节后有 13 个计数器字节，除最后的上报序号外均为小端序。
 * 让 HIL 不接调试器也能观察运行健康度。 */
static void cmd_stats(fw_demo_app_t *app, const fw_message_t *request,
                      fw_message_t *reply)
{
    if (request->length != 0U) {
        reply->payload[0] = 1U;
        return;
    }
    reply->length = 14U;
    put_u32_le(&reply->payload[1], app->frames);
    put_u32_le(&reply->payload[5], app->io_errors);
    put_u32_le(&reply->payload[9], app->partial_timeouts);
    reply->payload[13] = app->report_sequence;
}

/* 表驱动分发：新增命令只需增加一个处理函数和一行表项。
 * 未知命令仍返回统一的“不支持”状态。 */
static const fw_demo_command_t command_table[] = {
    { FW_CMD_PING, cmd_ping },
    { FW_CMD_LED_SET, cmd_led_set },
    { FW_CMD_STATUS, cmd_status },
    { FW_CMD_STATS, cmd_stats }
};

/* 从命令表生成一条回复并编码到 app->tx。
 * 对回复/事件帧不再回复，避免协议循环。 */
static fw_status_t handle_message(fw_demo_app_t *app, const fw_message_t *request)
{
    fw_message_t reply = {0};
    size_t i;
    size_t count = sizeof(command_table) / sizeof(command_table[0]);
    if ((request->command & 0x80U) != 0U) return FW_OK; /* 对回复帧绝不再次回复。 */
    reply.sequence = request->sequence;
    reply.command = (uint8_t)(request->command | 0x80U);
    reply.length = 1U;
    for (i = 0U; i < count; ++i) {
        if (command_table[i].command == request->command) {
            command_table[i].handler(app, request, &reply);
            break;
        }
    }
    if (i == count) reply.payload[0] = 2U; /* 未知命令。 */
    app->tx_offset = 0U;
    return fw_protocol_encode(&reply, app->tx, sizeof(app->tx), &app->tx_length);
}

/* 把待发送后缀交给流所有者。即使返回错误，accepted 也表示真实进展；
 * 调用者只重试 [tx_offset, tx_length) 区间。 */
static fw_status_t flush_tx(fw_demo_app_t *app)
{
    size_t accepted = 0U;
    fw_status_t result = fw_stream_write(&app->stream, &app->tx[app->tx_offset],
        app->tx_length - app->tx_offset, &accepted, app->tx_timeout_ms);
    app->tx_offset += accepted;
    if (app->tx_offset == app->tx_length) {
        app->tx_length = 0U;
        app->tx_offset = 0U;
    }
    if (result != FW_OK && result != FW_EAGAIN) ++app->io_errors;
    if (result == FW_OK && app->tx_length != 0U) return FW_EAGAIN;
    return result;
}

/* -------------------------------------------------------------------------
 * 公开 API
 * ------------------------------------------------------------------------- */
/* 初始化一个应用实例。stream/clock 必填；没有输出设备的板子可以传空
 * indicator。 */
fw_status_t fw_demo_init(fw_demo_app_t *app, fw_stream_t stream,
                         fw_indicator_t indicator, fw_clock_t clock)
{
    if (app == NULL || stream.ops == NULL || stream.ops->read == NULL ||
        stream.ops->write == NULL || clock.now_ms == NULL ||
        (indicator.ops != NULL && indicator.ops->set == NULL)) return FW_EINVAL;
    memset(app, 0, sizeof(*app));
    app->stream = stream;
    app->indicator = indicator;
    app->clock = clock;
    app->partial_timeout_ms = FW_DEMO_PARTIAL_TIMEOUT_MS;
    app->tx_timeout_ms = FW_DEMO_TX_TIMEOUT_MS;
    app->rx_budget = FW_DEMO_RX_BUDGET;
    fw_parser_init(&app->parser);
    return FW_OK;
}

/* 运行时策略覆盖。partial_timeout_ms=0 关闭超时；rx_budget 有上限，
 * 保证单轮 poll 不会独占任务。 */
fw_status_t fw_demo_set_limits(fw_demo_app_t *app, uint32_t partial_timeout_ms,
                               uint32_t tx_timeout_ms, uint32_t rx_budget)
{
    if (app == NULL || partial_timeout_ms > INT32_MAX ||
        rx_budget == 0U || rx_budget > FW_DEMO_MAX_RX_BUDGET) return FW_EINVAL;
    app->partial_timeout_ms = partial_timeout_ms;
    app->tx_timeout_ms = tx_timeout_ms;
    app->rx_budget = rx_budget;
    return FW_OK;
}

/* 设置主动上报周期，并以“当前时刻”重新基准。设 0 停止后续上报，
 * 但不会取消已经在途的帧。 */
fw_status_t fw_demo_set_report_period(fw_demo_app_t *app, uint32_t period_ms)
{
    if (app == NULL || app->clock.now_ms == NULL || period_ms > INT32_MAX)
        return FW_EINVAL;
    app->report_period_ms = period_ms;
    app->last_report_ms = app->clock.now_ms(app->clock.ctx);
    return FW_OK;
}

/* -------------------------------------------------------------------------
 * 上报与调度
 * ------------------------------------------------------------------------- */
/* 编码并交出一次主动状态帧。在 flush 前推进最后上报时间，
 * 让错过的周期合并，而不是排队成突发。 */
static fw_status_t report_status(fw_demo_app_t *app, uint32_t now)
{
    fw_message_t report = {0};
    fw_status_t result;
    report.command = FW_EVENT_STATUS;
    report.sequence = app->report_sequence;
    report.length = 3U;
    report.payload[1] = app->led_known ? 1U : 0U;
    report.payload[2] = app->led_requested_on ? 1U : 0U;
    result = fw_protocol_encode(&report, app->tx, sizeof(app->tx), &app->tx_length);
    if (result != FW_OK) return result;
    app->tx_offset = 0U;
    ++app->report_sequence;
    app->last_report_ms = now; /* 跳过错过的周期，绝不排队成突发。 */
    return flush_tx(app);
}

/* 返回拥有任务的睡眠预算：TX 未完成时 1 ms；期限已到时 0；
 * 否则返回最近的剩余期限。 */
uint32_t fw_demo_next_wait_ms(const fw_demo_app_t *app)
{
    uint32_t now;
    uint32_t wait = FW_DEMO_WAIT_FOREVER;
    uint32_t elapsed;
    uint32_t remaining;
    if (app == NULL || app->clock.now_ms == NULL) return 0U;
    if (app->tx_length != 0U) return 1U;
    now = app->clock.now_ms(app->clock.ctx);
    if (app->report_period_ms != 0U) {
        elapsed = fw_elapsed_ms(now, app->last_report_ms);
        remaining = elapsed >= app->report_period_ms ? 0U :
                    app->report_period_ms - elapsed;
        if (remaining < wait) wait = remaining;
    }
    if (app->partial_timeout_ms != 0U && fw_parser_partial(&app->parser)) {
        elapsed = fw_elapsed_ms(now, app->last_rx_ms);
        remaining = elapsed >= app->partial_timeout_ms ? 0U :
                    app->partial_timeout_ms - elapsed;
        if (remaining < wait) wait = remaining;
    }
    return wait;
}

/* 单拥有任务的一轮有界调度。
 *
 * 顺序很重要：先排空待发 TX，再有界消费 RX；仅当本轮没有读到任何字节时
 * 才让半帧超时；最后发送到期上报。这个顺序同时避免“上报周期饿死 RX”和
 * “超时误丢端口 ring 中已经完整的帧”。 */
fw_status_t fw_demo_poll(fw_demo_app_t *app)
{
    unsigned budget;
    uint8_t byte;
    size_t count;
    uint32_t now;
    uint32_t consumed = 0U;
    fw_status_t result;
    fw_message_t message;
    if (app == NULL || app->clock.now_ms == NULL) return FW_EINVAL;
    now = app->clock.now_ms(app->clock.ctx);
    /* 已经在途的帧必须先完成，才能复用其缓冲区。 */
    if (app->tx_length != 0U) return flush_tx(app);
    for (budget = 0U; budget < app->rx_budget; ++budget) {
        result = fw_stream_read(&app->stream, &byte, 1U, &count);
        if (result == FW_EAGAIN || (result == FW_OK && count == 0U)) break;
        if (result != FW_OK) {
            fw_parser_reset(&app->parser);
            ++app->io_errors;
            return result;
        }
        ++consumed;
        app->last_rx_ms = now;
        if (fw_parser_push(&app->parser, byte, &message)) {
            ++app->frames;
            result = handle_message(app, &message);
            if (result != FW_OK) return result;
            if (app->tx_length != 0U) {
                result = flush_tx(app);
                if (result != FW_OK) return result;
                break; /* 每轮最多发送一条回复；之后可以补发到期上报。 */
            }
        }
    }
    /* 只在整轮没有读到任何字节时才让半帧过期。端口里已有数据说明帧并未
     * 空闲，即使 parser 处于半帧状态；若先判超时再排空，会丢掉完整帧。 */
    if (consumed == 0U && app->partial_timeout_ms != 0U &&
        fw_parser_partial(&app->parser) &&
        fw_elapsed_ms(now, app->last_rx_ms) >= app->partial_timeout_ms) {
        fw_parser_reset(&app->parser);
        ++app->partial_timeouts;
    }
    /* RX 先获得有界机会，因此极小的上报周期不会饿死接收；
     * 上报仍每轮检查，因此连续请求也不会饿死上报。 */
    if (app->report_period_ms != 0U &&
        fw_elapsed_ms(now, app->last_report_ms) >= app->report_period_ms)
        return report_status(app, now);
    return FW_OK;
}
