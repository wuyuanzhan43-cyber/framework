/*
 * fw/freertos_runtime.h - 最小 FreeRTOS 运行时适配。
 *
 * 层级：port/freertos
 * 职责：通过框架中立类型暴露 tick 时间和任务休眠。
 * 上下文：sleep 仅任务上下文；clock 可在任务上下文读取。
 * 依赖：fw/io.h、FreeRTOS.h、task.h。
 */
#ifndef FW_FREERTOS_RUNTIME_H
#define FW_FREERTOS_RUNTIME_H

#include "fw/io.h"

/* 基于 xTaskGetTickCount() 的单调毫秒时钟。要求恰好 1 kHz tick
 * （runtime.c 中编译期检查）。 */
fw_clock_t fw_freertos_clock(void);

/* 让当前任务休眠；0 按一个 tick 处理，避免忙等。 */
void fw_freertos_sleep_ms(uint32_t milliseconds);

#endif
