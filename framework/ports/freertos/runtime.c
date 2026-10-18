/*
 * fw/freertos/runtime.c - tick 时钟与延时的适配。
 *
 * 层级：port/freertos
 * 职责：为装配代码提供一个时钟和一个休眠原语，不承载策略。
 * 上下文：任务上下文；clock 读取也可在普通任务中使用。
 * 依赖：FreeRTOS.h、task.h、fw/freertos_runtime.h。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "fw/freertos_runtime.h"

/* 框架把 tick 当作毫秒；不同的 tick 频率会静默改变所有超时和上报周期。 */
#if configTICK_RATE_HZ != 1000
#error "This initial runtime requires a 1 kHz tick for a modulo-2^32 millisecond clock."
#endif

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)xTaskGetTickCount();
}

fw_clock_t fw_freertos_clock(void)
{
    const fw_clock_t clock = { now_ms, NULL };
    return clock;
}

void fw_freertos_sleep_ms(uint32_t milliseconds)
{
    /* 零延时会令轮询循环空转；一个 tick 可以给其他任务运行机会。 */
    vTaskDelay((TickType_t)(milliseconds == 0U ? 1U : milliseconds));
}
