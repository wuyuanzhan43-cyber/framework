/*
 * stm32f4xx_hal_conf.h - 本项目的 HAL 功能选择。
 *
 * 层级：project/vendor 配置
 * 职责：只编译固件实际使用的 HAL 模块。
 * 上下文：编译期常量。
 * 依赖：STM32F4 HAL 头文件。
 */
#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

/* -------------------------------------------------------------------------
 * 启用的 HAL 模块（保持最小以控制代码体积）
 * ------------------------------------------------------------------------- */
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED

/* -------------------------------------------------------------------------
 * 时钟与板级常量
 * ------------------------------------------------------------------------- */
#define HSE_VALUE 8000000U /* 未使用：板级明确使用内部 HSI。 */
#define HSE_STARTUP_TIMEOUT 100U
#define HSI_VALUE 16000000U
#define LSI_VALUE 32000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define EXTERNAL_CLOCK_VALUE 12288000U
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 15U
#define USE_RTOS 0U /* HAL 配置标志；FreeRTOS 集成在外部完成。 */
#define PREFETCH_ENABLE 1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U

/* -------------------------------------------------------------------------
 * 模块头与断言 hook
 * ------------------------------------------------------------------------- */
#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_uart.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_pwr.h"

void fw_panic(const char *file, unsigned line);
#define assert_param(expr) ((expr) ? (void)0 : fw_panic(__FILE__, __LINE__))

#endif
