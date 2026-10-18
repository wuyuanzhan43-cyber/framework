/*
 * projects/host/main.c - 命令行 Host 演示。
 *
 * 层级：project/host
 * 职责：展示一次主动上报，然后展示一次 LED_SET 请求/回复。
 * 上下文：单线程测试程序。
 * 依赖：fw/demo_app.h、fw/host.h、stdio.h。
 */
#include "fw/demo_app.h"
#include "fw/host.h"
#include <stdio.h>

int main(void)
{
    fw_host_t host;
    fw_demo_app_t app;
    fw_message_t request = {0};
    uint8_t frame[FW_PROTOCOL_MAX_FRAME];
    size_t size;
    size_t i;
    fw_host_init(&host);
    if (fw_demo_init(&app, fw_host_stream(&host), fw_host_indicator(&host),
                     fw_host_clock(&host)) != FW_OK) return 1;
    if (fw_demo_set_report_period(&app, FW_DEMO_REPORT_PERIOD_MS) != FW_OK) return 1;
    /* 模拟一个没有 RX 的上报周期，触发主动状态帧。 */
    host.now_ms += FW_DEMO_REPORT_PERIOD_MS;
    if (fw_demo_poll(&app) != FW_OK) return 1;
    size = fw_host_take_tx(&host, frame, sizeof(frame));
    if (size == 0U) return 1;
    printf("autonomous (no request):");
    for (i = 0U; i < size; ++i) printf(" %02X", (unsigned)frame[i]);
    putchar('\n');
    /* 通过与固件相同的 stream 注入一次 LED_SET 请求。 */
    request.sequence = 1U;
    request.command = FW_CMD_LED_SET;
    request.length = 1U;
    request.payload[0] = 1U;
    if (fw_protocol_encode(&request, frame, sizeof(frame), &size) != FW_OK) return 1;
    printf("request:");
    for (i = 0U; i < size; ++i) printf(" %02X", (unsigned)frame[i]);
    putchar('\n');
    if (fw_host_receive(&host, frame, size) != size || fw_demo_poll(&app) != FW_OK) return 1;
    size = fw_host_take_tx(&host, frame, sizeof(frame));
    printf("reply:  ");
    for (i = 0U; i < size; ++i) printf(" %02X", (unsigned)frame[i]);
    /* 退出码让 CTest 把缺少回复或 LED 故障判为失败。 */
    printf("\nLED=%s; accepted requests=%lu\n", host.led_on ? "ON" : "OFF",
           (unsigned long)app.frames);
    return host.led_on && size != 0U ? 0 : 1;
}
