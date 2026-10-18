/*
 * board.h - STM32F407VE 板级 profile。
 *
 * 层级：board
 * 职责：拥有时钟、引脚、设备实例和板级 hook。
 * 上下文：init 在任务上下文；UART hook 在 ISR 上下文。
 * 依赖：fw/stm32f4.h、board_config.h。
 */
#ifndef BOARD_H
#define BOARD_H

#include "fw/stm32f4.h"
#include "fw/st7735.h"

typedef struct {
    bool indicator; /* 编译并初始化了 LED/输出设备 */
    bool console;   /* 编译了 UART 控制台 */
} board_caps_t;

/* 时钟和 LED。不触碰串口；应用确实需要串口时再调用
 * board_console_init()。 */
fw_status_t board_init(void);

/* 配置 UART 引脚/时钟/DMA 并初始化 console 实例。
 * 板级 profile 禁用串口时返回 ENOTREADY。 */
fw_status_t board_console_init(void);

/* 用单字节 IT 或配置的 DMA 映射 arm RX。 */
fw_status_t board_start_console(void);

/* 重试停滞的 TX 启动；正常情况下由完成 ISR 泵 ring。 */
fw_status_t board_console_pump(void);

/* 初始化 1.8 寸 ST7735S SPI TFT；未启用或接线失败时返回错误。
 * 成功后 board_lcd() 返回驱动上下文，board_lcd_demo() 可画色条。 */
fw_status_t board_lcd_init(void);
st7735_t *board_lcd(void);
void board_lcd_demo(void);

/* 可选独立看门狗；禁用时所有函数为空操作。 */
void board_watchdog_init(void);
void board_watchdog_refresh(void);

/* 编译期板级能力，供装配代码使用。 */
board_caps_t board_capabilities(void);

/* 设备视图。缺少硬件时返回空 indicator 或会报告 ENOTREADY 的 stream，
 * 而不是让 board init 直接失败。 */
fw_indicator_t board_indicator(void);
fw_stream_t board_console(void);
fw_stm32_uart_t *board_uart_context(void);

/* ISR 侧 hook。必须短小且 ISR 安全（例如 FreeRTOS 任务通知）。
 * board_uart_*_event() 由 UART ISR 调用。 */
void board_set_uart_hooks(void (*rx_event)(void), void (*tx_event)(void));
void board_uart_rx_event(void);
void board_uart_tx_event(void);

#endif
