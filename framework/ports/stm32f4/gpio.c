/*
 * fw/stm32f4/gpio.c - GPIO 指示器适配。
 *
 * 层级：port/stm32f4
 * 职责：把逻辑开/关请求映射到一个推挽 GPIO 引脚。
 * 上下文：仅任务；应用拥有该设备。
 * 依赖：fw/stm32f4.h。
 */
#include "fw/stm32f4.h"

/* 按板级的高有效/低有效约定施加请求。 */
static fw_status_t set_output(void *ctx, bool on)
{
    fw_stm32_indicator_t *output = ctx;
    if (output == NULL || !output->ready || output->gpio == NULL || output->pin == 0U) {
        return FW_ENOTREADY;
    }
    HAL_GPIO_WritePin(output->gpio, output->pin,
        on == output->active_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return FW_OK;
}

/* 在调用者拥有的 GPIO 上下文上构建设备视图。 */
fw_indicator_t fw_stm32_indicator(fw_stm32_indicator_t *context)
{
    static const fw_indicator_ops_t ops = { set_output };
    const fw_indicator_t device = { &ops, context };
    return device;
}
