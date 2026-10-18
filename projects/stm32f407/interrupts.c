/*
 * interrupts.c - 向量处理、UART/DMA 回调与故障捕获。
 *
 * 层级：project
 * 职责：把 vendor 向量表连接到板级/端口回调。
 * 上下文：本文件中的处理函数全部运行在 ISR 上下文。
 * 依赖：FreeRTOS、board.h、board_config.h。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "board_config.h"

extern void xPortSysTickHandler(void);

/* 一个 SysTick 同时服务 HAL 时间和 FreeRTOS 调度器。 */
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xPortSysTickHandler();
}

/* 把共享向量转发给唯一的板级 console 实例。 */
static void dispatch_uart(USART_TypeDef *instance)
{
    fw_stm32_uart_t *context = board_uart_context();
    if (context->initialized && context->hal.Instance == instance) {
        HAL_UART_IRQHandler(&context->hal);
    }
}

void USART1_IRQHandler(void) { dispatch_uart(USART1); }
void USART2_IRQHandler(void) { dispatch_uart(USART2); }
void USART3_IRQHandler(void) { dispatch_uart(USART3); }
/* IT RX：发布一个字节，然后唤醒拥有任务。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    fw_stm32_uart_rx_complete(board_uart_context(), uart);
    board_uart_rx_event();
}
/* DMA 循环 RX：HT/TC/IDLE 事件；复制增量并唤醒任务。 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    fw_stm32_uart_t *context = board_uart_context();
    (void)uart;
    if (context->initialized && context->rx_dma_enabled) {
        fw_stm32_uart_rx_event(context, size);
        board_uart_rx_event();
    }
}
/* TX 完成：消费 chunk，泵下一段并唤醒任务。 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    fw_stm32_uart_tx_complete(board_uart_context(), uart);
    board_uart_tx_event();
}
/* UART 错误：在端口中分类，然后唤醒两个方向，
 * 让任务不必等待空闲兜底就能重新 arm 或重试。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    fw_stm32_uart_error(board_uart_context(), uart);
    board_uart_rx_event();
    board_uart_tx_event();
}
#if BOARD_UART_DMA_ENABLED
/* DMA 向量处理：保持薄，交给 HAL 分发到 UART 回调。 */
void BOARD_UART_RX_DMA_IRQ_HANDLER(void)
{
    fw_stm32_uart_t *context = board_uart_context();
    if (context->initialized && context->rx_dma_enabled) {
        HAL_DMA_IRQHandler(&context->hdma_rx);
    }
}
void BOARD_UART_TX_DMA_IRQ_HANDLER(void)
{
    fw_stm32_uart_t *context = board_uart_context();
    if (context->initialized && context->tx_dma_enabled) {
        HAL_DMA_IRQHandler(&context->hdma_tx);
    }
}
#endif
/* -------------------------------------------------------------------------
 * 故障捕获
 *
 * dump 位于 .bss，看门狗复位会清空；调试时保持
 * BOARD_IWDG_ENABLED=0，或在复位前读取。
 * ------------------------------------------------------------------------- */
#define FW_FAULT_MAGIC 0x4641554CU /* "FAUL" */
typedef struct {
    uint32_t magic;       /* 记录故障后写入 FW_FAULT_MAGIC */
    uint32_t fault_type;  /* 1 HardFault、2 MemManage、3 BusFault、4 UsageFault */
    uint32_t exc_return;  /* 进入时的 Cortex-M EXC_RETURN；回退路径为 0 */
    uint32_t cfsr;        /* 可配置故障状态寄存器 */
    uint32_t hfsr;        /* HardFault 状态寄存器 */
    uint32_t mmfar;       /* MemManage 故障地址 */
    uint32_t bfar;        /* BusFault 地址 */
    uint32_t msp;         /* 捕获时的主栈指针 */
    uint32_t psp;         /* 捕获时的进程栈指针 */
    uint32_t ipsr;        /* 捕获时的异常号 */
    bool frame_valid;     /* frame[] 来自异常栈时为 true */
    uint32_t frame[8];    /* r0、r1、r2、r3、r12、lr、pc、xpsr */
} fw_fault_dump_t;

volatile fw_fault_dump_t g_fault_dump;

/* 保存故障状态，并在可用时保存异常栈帧。
 * 不返回：故意让板子停住以便调试。 */
static void fault_record(uint32_t type, uint32_t exc_return, const uint32_t *frame)
{
    unsigned i;
    g_fault_dump.magic = FW_FAULT_MAGIC;
    g_fault_dump.fault_type = type;
    g_fault_dump.exc_return = exc_return;
    g_fault_dump.cfsr = SCB->CFSR;
    g_fault_dump.hfsr = SCB->HFSR;
    g_fault_dump.mmfar = SCB->MMFAR;
    g_fault_dump.bfar = SCB->BFAR;
    g_fault_dump.msp = __get_MSP();
    g_fault_dump.psp = __get_PSP();
    g_fault_dump.ipsr = __get_IPSR();
    g_fault_dump.frame_valid = frame != NULL;
    for (i = 0U; i < 8U; ++i)
        g_fault_dump.frame[i] = frame != NULL ? frame[i] : 0U;
    for (;;) { __NOP(); }
}

#if defined(__GNUC__)
/* GCC 路径：把异常栈帧（MSP 或 PSP）和 EXC_RETURN 传入 C 记录器。
 * 这样可以恢复出错 PC/LR，普通 C handler 会丢失它们。 */
void fw_fault_c_handler(uint32_t *frame, uint32_t exc_return);
__attribute__((used))
void fw_fault_c_handler(uint32_t *frame, uint32_t exc_return)
{
    fault_record(1U, exc_return, frame);
}
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n\t"
        "ite eq\n\t"
        "mrseq r0, msp\n\t"
        "mrsne r0, psp\n\t"
        "mov r1, lr\n\t"
        "b fw_fault_c_handler\n\t");
}
#else
/* AC5 回退：没有 naked 包装，只能捕获寄存器。 */
void HardFault_Handler(void) { fault_record(1U, 0U, NULL); }
#endif
/* 其他故障类型共用同一记录；这里没有异常栈帧。 */
void MemManage_Handler(void) { fault_record(2U, 0U, NULL); }
void BusFault_Handler(void) { fault_record(3U, 0U, NULL); }
void UsageFault_Handler(void) { fault_record(4U, 0U, NULL); }
