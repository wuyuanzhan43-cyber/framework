/*
 * fw/stm32f4.h - STM32F4 端口上下文与回调。
 *
 * 层级：port/stm32f4
 * 职责：把 fw_stream/fw_indicator 绑定到 UART/GPIO 的 HAL 对象。
 * 上下文：配置在任务/初始化阶段；RX/TX 完成回调在 ISR 中。
 * 依赖：fw/io.h、fw/ring.h、stm32f4xx_hal.h。
 *
 * 平台私有头：不要从 framework/app 或 framework/components 引用。
 */
#ifndef FW_STM32F4_H
#define FW_STM32F4_H

#include "stm32f4xx_hal.h"
#include "fw/io.h"
#include "fw/ring.h"

typedef struct {
    GPIO_TypeDef *gpio;  /* 拥有输出引脚的端口 */
    uint16_t pin;        /* GPIO_PIN_x */
    bool active_high;    /* true：引脚输出高电平表示打开 */
    bool ready;          /* board_init() 已配置该引脚 */
} fw_stm32_indicator_t;

/* 构建系统在启用 DMA 时定义为 1。没有该宏的直接/手工构建会保留中断路径，
 * 使板和端口行为保持一致。 */
#ifndef FW_STM32_UART_DMA
#define FW_STM32_UART_DMA 0
#endif

#define FW_STM32_UART_TX_CHUNK 64U   /* 每次 DMA/IT 传输的最大连续字节数 */
#define FW_STM32_UART_DMA_RX_SIZE 256U /* 主 SRAM 中的 DMA 循环接收缓冲 */

/* 可选 DMA 映射。由板级使能 DMA 控制器时钟和中断；端口负责初始化句柄
 * 并链接到 UART。传 NULL 则保留中断驱动的 RX/TX 路径。 */
typedef struct {
    DMA_Stream_TypeDef *rx_stream;  /* 外设到存储器的接收 stream */
    uint32_t rx_channel;            /* USART RX 请求对应的 DMA_CHANNEL_x */
    DMA_Stream_TypeDef *tx_stream;  /* 存储器到外设的发送 stream */
    uint32_t tx_channel;            /* USART TX 请求对应的 DMA_CHANNEL_x */
} fw_stm32_uart_dma_t;

typedef struct {
    UART_HandleTypeDef hal;  /* HAL 拥有的 UART 状态；由 init 配置 */
    fw_ring_t rx;            /* ISR/DMA 生产，任务消费 */
    fw_ring_t tx;            /* 任务生产，发送完成 ISR 消费 */
    DMA_HandleTypeDef hdma_rx;  /* rx_dma_enabled 为 true 时有效 */
    DMA_HandleTypeDef hdma_tx;  /* tx_dma_enabled 为 true 时有效 */

    uint8_t rx_byte;         /* 单字节 IT 回退的落点 */
    uint8_t rx_dma_storage[FW_STM32_UART_DMA_RX_SIZE]; /* DMA 循环接收缓冲 */
    volatile uint16_t rx_dma_pos; /* 最近复制位置；ISR 写，任务复位 */

    uint8_t tx_chunk[FW_STM32_UART_TX_CHUNK]; /* 发送完成前保持稳定 */
    uint16_t tx_inflight;    /* 当前正在发送的 tx_chunk 字节数 */
    volatile bool rx_dma_enabled; /* DMA 句柄已链接并启动 */
    volatile bool tx_dma_enabled; /* 发送路径使用 DMA 而不是 IT */
    volatile bool tx_active;      /* 有一个 chunk 正在传输 */

    volatile bool loss;      /* RX 数据已丢弃；任务上报 EOVERFLOW */
    volatile bool stopped;   /* 接收器出错后需要重新 arm */
    volatile uint32_t dropped;          /* RX ring 满导致丢失的字节数 */
    volatile uint32_t errors;           /* HAL UART 错误回调总次数 */
    volatile uint32_t rx_line_errors;   /* FE/NE/PE：一个坏字节 */
    volatile uint32_t rx_overrun_errors;/* ORE：流连续性丢失 */
    volatile uint32_t tx_errors;        /* 启动/中止/消费失败次数 */
    /* 在使能 UART 中断前设置；IRQ/stream 活动期间不可变。 */
    bool initialized;
    /* 由 fw_stm32_uart_start() 设置。在此之前 RXNE 未 arm，读操作属于调用顺序
     * 错误而不是链路丢失：read 必须返回 ENOTREADY，不得清空 ring 或操作 HAL。
     * 只在任务上下文中写入一次，然后把 stream 交给拥有任务。 */
    bool started;
} fw_stm32_uart_t;

/* 把已配置的 GPIO 输出包装成通用指示器。 */
fw_indicator_t fw_stm32_indicator(fw_stm32_indicator_t *context);
/* 板级先配置时钟/引脚再 init，随后配置 NVIC 再 start。
 * 必须在使能 UART 中断前只初始化一次；stream 活动期间不支持并发 re-init、
 * start 或其他任务访问。RX/TX 存储由调用者拥有，生命周期必须覆盖上下文。
 *
 * read 在 PRIMASK 下复制 RAM（最多 64 字节），从不等待线路。
 * write 复制到 TX ring 并返回 accepted；HAL DMA/IT 在后台排空。accepted
 * 仍不承诺物理线路完成，符合 fw/io.h。
 * 启动失败后由拥有任务调用 fw_stm32_uart_pump()；发送完成中断也会泵下一段。 */
fw_status_t fw_stm32_uart_init(fw_stm32_uart_t *context, USART_TypeDef *instance,
                               uint32_t baud, uint8_t *rx_storage, size_t rx_capacity,
                               uint8_t *tx_storage, size_t tx_capacity,
                               const fw_stm32_uart_dma_t *dma);
/* arm RX（DMA 循环或 IT）并标记 stream 可用。接收器保持 armed 时幂等；
 * 在 board init 之后由任务上下文调用。 */
fw_status_t fw_stm32_uart_start(fw_stm32_uart_t *context);

/* ring 非空且没有传输进行时，启动下一个 TX chunk。
 * 任务或 TX 完成 ISR 均可安全调用；HAL 忙时返回 EAGAIN。 */
fw_status_t fw_stm32_uart_pump(fw_stm32_uart_t *context);

/* 返回通用 stream 视图；上下文生命周期必须覆盖 stream。 */
fw_stream_t fw_stm32_uart_stream(fw_stm32_uart_t *context);

/* IT 回退的 HAL RX 回调：写入一个字节并重新 arm RXNE。
 * 上下文：仅 ISR。 */
void fw_stm32_uart_rx_complete(fw_stm32_uart_t *context, UART_HandleTypeDef *uart);

/* DMA 循环模式的 HAL RxEvent 回调：把 HT/TC/IDLE 差值复制到 RX ring。
 * 上下文：仅 ISR。 */
void fw_stm32_uart_rx_event(fw_stm32_uart_t *context, uint16_t size);

/* HAL TX 回调：消费已发送 chunk，并泵下一段。
 * 上下文：仅 ISR。 */
void fw_stm32_uart_tx_complete(fw_stm32_uart_t *context, UART_HandleTypeDef *uart);

/* HAL 错误回调：区分 ORE 与线路错误，并标记 RX 需要重新 arm。
 * 上下文：仅 ISR。 */
void fw_stm32_uart_error(fw_stm32_uart_t *context, UART_HandleTypeDef *uart);

#endif
