/*
 * FreeRTOSConfig.h - 本项目的内核配置。
 *
 * 层级：project/RTOS 配置
 * 职责：单应用任务 + 空闲任务的纯静态调度器。
 * 上下文：编译期常量。
 * 依赖：stdint.h；SystemCoreClock 和 fw_panic 由项目提供。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;
void fw_panic(const char *file, unsigned line);

/* -------------------------------------------------------------------------
 * 调度器与任务形态
 * ------------------------------------------------------------------------- */
#define configUSE_PREEMPTION 1
#define configCPU_CLOCK_HZ SystemCoreClock
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 256
#define configMAX_TASK_NAME_LEN 16
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
/* UART ISR 到应用任务的唤醒使用任务通知。 */
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_QUEUE_SETS 0
#define configUSE_TIMERS 0
#define configUSE_CO_ROUTINES 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
/* 纯静态项目：无堆、无定时器任务、无 idle/tick hook。 */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
/* -------------------------------------------------------------------------
 * Cortex-M 中断优先级
 * ------------------------------------------------------------------------- */
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
/* 这两个宏会展开到端口层的内联汇编
 * （mov r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY）。ARM Compiler 5 的
 * armasm 无法解析 C 的 "U" 后缀，因此不带后缀保存最终值。中断优先级是
 * 无符号 8 位数，C 调用者会隐式转换，后缀没有额外收益。普通整数字面量
 * 也让这些表达式可用于汇编器和任何编译器。 */
#ifndef configKERNEL_INTERRUPT_PRIORITY
#define configKERNEL_INTERRUPT_PRIORITY (15 << (8 - configPRIO_BITS))
#endif
#ifndef configMAX_SYSCALL_INTERRUPT_PRIORITY
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))
#endif
/* -------------------------------------------------------------------------
 * Hook 与必需的内核入口
 * ------------------------------------------------------------------------- */
#define configASSERT(expr) ((expr) ? (void)0 : fw_panic(__FILE__, __LINE__))
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* SysTick_Handler 在 interrupts.c 中显式分发 HAL + FreeRTOS。 */

#endif
