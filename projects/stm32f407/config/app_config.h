/*
  * app_config.h - 项目级模式与策略。
  *
  * 层级：project
  * 职责：选择 bring-up 或协议固件，并调整轮询策略。
  * 上下文：编译期常量，无运行时状态。
  * 依赖：无（由 project/main.c 引入）。
  */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* -------------------------------------------------------------------------
 * 固件模式
 *
 * 1：仅 UART bring-up。立即发送 hello 文本，之后每秒一次。
 *    在启动 FreeRTOS 之前运行，不启用 RX 中断或二进制协议。
 * 0：协议应用，启用 FreeRTOS、命令和周期状态帧。
 * ------------------------------------------------------------------------- */
#define APP_UART_HELLO_TEST 0
#define APP_UART_HELLO_PERIOD_MS 1000U

/* -------------------------------------------------------------------------
 * 应用策略，在 fw_demo_init() 之后应用。
 *
 * 半帧超时必须大于一帧的最坏传输时间：
 * FW_PROTOCOL_MAX_FRAME（39）× 10 bit/字节 ÷ 波特率。115200 下约 3.4 ms；
 * 250 ms 余量充足。1200 baud 下一帧约 325 ms，建议至少 400 ms。
 * 设为 0 可关闭半帧超时。
 * ------------------------------------------------------------------------- */
#define APP_DEMO_PARTIAL_TIMEOUT_MS 250U
#define APP_DEMO_TX_TIMEOUT_MS      20U
#define APP_DEMO_RX_BUDGET          64U
/* 事件驱动循环的活性兜底。真实 RX/TX 事件会立即唤醒任务；
 * 这里只限制漏通知或端口停滞的等待时间。 */
#define APP_EVENT_IDLE_WAIT_MS      1000U

#endif
