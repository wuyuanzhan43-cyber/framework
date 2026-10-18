/*
 * fw/stm32f4/uart.c - 带 IT/DMA 接收和 TX ring 的 USART 字节流。
 *
 * 层级：port/stm32f4
 * 职责：把一个 USART 包装成 fw_stream_t，并保持 ISR/任务边界。
 * 上下文：任务拥有 read/write；RX 完成是 ISR 生产者，TX 完成是 ISR 消费者。
 *         共享 ring 状态由 PRIMASK 保护。
 * 依赖：fw/stm32f4.h、string.h。
 */
#include "fw/stm32f4.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * 临界区
 * ------------------------------------------------------------------------- */
static uint32_t lock_rx(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return mask;
}

static void unlock_rx(uint32_t mask)
{
    __DMB();
    __set_PRIMASK(mask);
}

/* TX ring 有一个任务侧生产者（write）和一个 ISR 侧消费者（发送完成）。
 * 两个索引和 count 都在 PRIMASK 下修改。 */
static uint32_t lock_tx(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return mask;
}

static void unlock_tx(uint32_t mask)
{
    __DMB();
    __set_PRIMASK(mask);
}

/* 按配置模式 arm 接收器。调用者必须持有 lock_rx()。 */
static HAL_StatusTypeDef arm_rx(fw_stm32_uart_t *uart)
{
#if FW_STM32_UART_DMA
    if (uart->rx_dma_enabled) {
        uart->rx_dma_pos = 0U;
        return HAL_UARTEx_ReceiveToIdle_DMA(&uart->hal, uart->rx_dma_storage,
                                            (uint16_t)sizeof(uart->rx_dma_storage));
    }
#endif
    return HAL_UART_Receive_IT(&uart->hal, &uart->rx_byte, 1U);
}

/* 任务侧读取：只复制已缓存字节，绝不等待线路。
 * EOVERFLOW 表示连续性丢失且 ring 已清空；EAGAIN 仅表示暂时没有数据。 */
static fw_status_t read_bytes(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    fw_stm32_uart_t *uart = ctx;
    uint32_t mask;
    *count = 0U;
    /* “尚未启动”是调用顺序错误，不是链路丢失。不能让这种情况落入下面的
     * loss/stopped 分支，否则会清空 ring 并重新 arm RXNE。 */
    if (uart == NULL || !uart->initialized || !uart->started) return FW_ENOTREADY;
    /* 有界的任务侧临界区。IRQ 是唯一生产者，任务是消费者。
     * 最多复制 64 字节，即使调用者缓冲更大，也限制关中断时间。 */
    if (capacity > 64U) capacity = 64U;
    mask = lock_rx();
    if (uart->stopped) {
        /* 线路错误（FE/NE/PE）不会让已缓存字节失效。
         * 只有溢出/连续丢失标志才需要丢弃整条流。 */
        bool had_loss = uart->loss;
        uart->loss = false;
        (void)HAL_UART_AbortReceive(&uart->hal);
        uart->stopped = arm_rx(uart) != HAL_OK;
        if (had_loss) {
            fw_ring_clear(&uart->rx);
            unlock_rx(mask);
            return FW_EOVERFLOW;
        }
    } else if (uart->loss) {
        uart->loss = false;
        fw_ring_clear(&uart->rx);
        unlock_rx(mask);
        return FW_EOVERFLOW;
    }
    *count = fw_ring_read(&uart->rx, data, capacity);
    unlock_rx(mask);
    return *count == 0U ? FW_EAGAIN : FW_OK;
}

/* 启动一个连续 TX chunk。peek/consume 分离保证 chunk 在完成回调推进
 * ring tail 之前保持稳定。 */
fw_status_t fw_stm32_uart_pump(fw_stm32_uart_t *context)
{
    uint32_t mask;
    size_t chunk;
    HAL_StatusTypeDef result;
    if (context == NULL || !context->initialized) return FW_ENOTREADY;
    mask = lock_tx();
    if (context->tx_active || context->tx.count == 0U) {
        unlock_tx(mask);
        return FW_OK;
    }
    /* 只 peek，不消费：这些字节仍归 ring 所有，直到完成回调确认该 chunk
     * 已经发送。 */
    chunk = fw_ring_peek(&context->tx, context->tx_chunk, sizeof(context->tx_chunk));
    if (chunk == 0U) {
        unlock_tx(mask);
        return FW_OK;
    }
    context->tx_active = true;
    context->tx_inflight = (uint16_t)chunk;
    unlock_tx(mask);
#if FW_STM32_UART_DMA
    result = context->tx_dma_enabled
        ? HAL_UART_Transmit_DMA(&context->hal, context->tx_chunk, (uint16_t)chunk)
        : HAL_UART_Transmit_IT(&context->hal, context->tx_chunk, (uint16_t)chunk);
#else
    result = HAL_UART_Transmit_IT(&context->hal, context->tx_chunk, (uint16_t)chunk);
#endif
    if (result != HAL_OK) {
        mask = lock_tx();
        context->tx_active = false;
        context->tx_inflight = 0U;
        ++context->tx_errors;
        unlock_tx(mask);
        return result == HAL_BUSY ? FW_EAGAIN : FW_EIO;
    }
    return FW_OK;
}

/* 任务侧写入：复制进 ring 并返回 accepted 进展。timeout 参数未使用，
 * 因为接受是立即的；物理完成由 TX 回调异步报告。 */
static fw_status_t write_bytes(void *ctx, const uint8_t *data, size_t length,
                               size_t *count, uint32_t timeout_ms)
{
    fw_stm32_uart_t *uart = ctx;
    uint32_t mask;
    size_t accepted;
    (void)timeout_ms; /* 接受是立即的；完成是异步的。 */
    *count = 0U;
    if (uart == NULL || !uart->initialized) return FW_ENOTREADY;
    if (__get_IPSR() != 0U) return FW_EINVAL;
    mask = lock_tx();
    accepted = fw_ring_write(&uart->tx, data, length);
    unlock_tx(mask);
    *count = accepted;
    (void)fw_stm32_uart_pump(uart);
    return accepted < length ? FW_EAGAIN : FW_OK;
}

/* 初始化 UART；提供 DMA 映射时同时链接两个 DMA 句柄。
 * 存储由调用者拥有，生命周期必须覆盖上下文。dma 为 NULL 时选择 IT 回退。 */
fw_status_t fw_stm32_uart_init(fw_stm32_uart_t *context, USART_TypeDef *instance,
                               uint32_t baud, uint8_t *rx_storage, size_t rx_capacity,
                               uint8_t *tx_storage, size_t tx_capacity,
                               const fw_stm32_uart_dma_t *dma)
{
    if (context == NULL || instance == NULL || baud == 0U ||
        rx_storage == NULL || rx_capacity == 0U ||
        tx_storage == NULL || tx_capacity == 0U) return FW_EINVAL;
#if !FW_STM32_UART_DMA
    (void)dma;
#endif
    memset(context, 0, sizeof(*context));
    if (fw_ring_init(&context->rx, rx_storage, rx_capacity) != FW_OK) return FW_EINVAL;
    if (fw_ring_init(&context->tx, tx_storage, tx_capacity) != FW_OK) return FW_EINVAL;
    context->hal.Instance = instance;
    context->hal.Init.BaudRate = baud;
    context->hal.Init.WordLength = UART_WORDLENGTH_8B;
    context->hal.Init.StopBits = UART_STOPBITS_1;
    context->hal.Init.Parity = UART_PARITY_NONE;
    context->hal.Init.Mode = UART_MODE_TX_RX;
    context->hal.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    context->hal.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&context->hal) != HAL_OK) return FW_EIO;
#if FW_STM32_UART_DMA
    if (dma != NULL && dma->rx_stream != NULL && dma->tx_stream != NULL) {
        context->hdma_rx.Instance = dma->rx_stream;
        context->hdma_rx.Init.Channel = dma->rx_channel;
        context->hdma_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        context->hdma_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        context->hdma_rx.Init.MemInc = DMA_MINC_ENABLE;
        context->hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        context->hdma_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        context->hdma_rx.Init.Mode = DMA_CIRCULAR;
        context->hdma_rx.Init.Priority = DMA_PRIORITY_HIGH;
        context->hdma_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        context->hdma_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        context->hdma_rx.Init.MemBurst = DMA_MBURST_SINGLE;
        context->hdma_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        if (HAL_DMA_Init(&context->hdma_rx) != HAL_OK) return FW_EIO;
        __HAL_LINKDMA(&context->hal, hdmarx, context->hdma_rx);

        context->hdma_tx.Instance = dma->tx_stream;
        context->hdma_tx.Init.Channel = dma->tx_channel;
        context->hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        context->hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        context->hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
        context->hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        context->hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        context->hdma_tx.Init.Mode = DMA_NORMAL;
        context->hdma_tx.Init.Priority = DMA_PRIORITY_HIGH;
        context->hdma_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        context->hdma_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        context->hdma_tx.Init.MemBurst = DMA_MBURST_SINGLE;
        context->hdma_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        if (HAL_DMA_Init(&context->hdma_tx) != HAL_OK) return FW_EIO;
        __HAL_LINKDMA(&context->hal, hdmatx, context->hdma_tx);
        context->rx_dma_enabled = true;
        context->tx_dma_enabled = true;
    }
#endif
    context->initialized = true;
    context->stopped = true;
    return FW_OK;
}

/* 只 arm 一次 RX；接收器保持 armed 时重复调用无害。 */
fw_status_t fw_stm32_uart_start(fw_stm32_uart_t *context)
{
    if (context == NULL || !context->initialized) return FW_ENOTREADY;
    /* 幂等：对已 armed 的接收器再次调用 HAL_UART_Receive_IT 会返回 HAL_BUSY，
     * 若误判为 arm 失败会静默停止数据流。 */
    if (context->started && !context->stopped) return FW_OK;
    /* 在 arm 之前标记 started，使观察 post-arm 状态的 read 能看到“已启动”，
     * 从而尝试恢复失败的 arm，而不是报告“尚未启动”。 */
    context->started = true;
    context->stopped = arm_rx(context) != HAL_OK;
    return context->stopped ? FW_EIO : FW_OK;
}

/* 返回通用 stream 视图；ops 是静态的，上下文由调用者拥有。 */
fw_stream_t fw_stm32_uart_stream(fw_stm32_uart_t *context)
{
    static const fw_stream_ops_t ops = { read_bytes, write_bytes };
    const fw_stream_t stream = { &ops, context };
    return stream;
}

#if FW_STM32_UART_DMA
/* 把一个连续 DMA 缓冲片段复制到 RX ring。短写表示 ring 溢出；
 * 记录 loss，让 parser 重新同步。 */
static void rx_dma_copy(fw_stm32_uart_t *context, uint16_t from, uint16_t to)
{
    size_t length = (size_t)(to - from);
    size_t written;
    if (length == 0U) return;
    written = fw_ring_write(&context->rx, &context->rx_dma_storage[from], length);
    if (written != length) {
        context->loss = true;
        context->dropped += (uint32_t)(length - written);
    }
}
#endif

/* IT 回退：收到一个字节；发布它并重新 arm RXNE。
 * 上下文：UART ISR。 */
void fw_stm32_uart_rx_complete(fw_stm32_uart_t *context, UART_HandleTypeDef *uart)
{
    if (context == NULL || uart != &context->hal || !context->initialized) return;
    if (fw_ring_write(&context->rx, &context->rx_byte, 1U) != 1U) {
        context->loss = true;
        ++context->dropped;
    }
    context->stopped = HAL_UART_Receive_IT(uart, &context->rx_byte, 1U) != HAL_OK;
}

/* DMA 循环事件：HT/TC/IDLE 报告一个缓冲位置。只复制相对上次位置的增量，
 * 跨 wrap 时分两段复制。上下文：UART/DMA ISR。 */
void fw_stm32_uart_rx_event(fw_stm32_uart_t *context, uint16_t size)
{
#if FW_STM32_UART_DMA
    uint16_t capacity;
    uint16_t position;
    uint16_t last;
    uint32_t event;
    if (context == NULL || !context->initialized || !context->rx_dma_enabled) return;
    capacity = (uint16_t)sizeof(context->rx_dma_storage);
    event = (uint32_t)HAL_UARTEx_GetRxEventType(&context->hal);
    if (event == (uint32_t)HAL_UART_RXEVENT_TC) {
        position = capacity;
    } else if (event == (uint32_t)HAL_UART_RXEVENT_HT) {
        position = capacity / 2U;
    } else {
        position = size;
    }
    if (position > capacity) position = capacity;
    last = context->rx_dma_pos;
    if (position == last) return;
    if (position > last) {
        rx_dma_copy(context, last, position);
    } else {
        rx_dma_copy(context, last, capacity);
        if (position > 0U) rx_dma_copy(context, 0U, position);
    }
    /* 缓冲区满时报告 TC；下一个位置从 0 开始。 */
    context->rx_dma_pos = (position == capacity) ? 0U : position;
#else
    (void)context;
    (void)size;
#endif
}

/* 一个 chunk 完成：精确消费在途字节，然后启动下一段。
 * 上下文：UART/DMA ISR。 */
void fw_stm32_uart_tx_complete(fw_stm32_uart_t *context, UART_HandleTypeDef *uart)
{
    uint32_t mask;
    size_t consumed;
    if (context == NULL || uart != &context->hal || !context->initialized) return;
    mask = lock_tx();
    consumed = fw_ring_read(&context->tx, context->tx_chunk, context->tx_inflight);
    if (consumed != context->tx_inflight) ++context->tx_errors;
    context->tx_inflight = 0U;
    context->tx_active = false;
    unlock_tx(mask);
    /* 如果还有下一段连续 chunk，则启动它；ISR 上下文安全。 */
    (void)fw_stm32_uart_pump(context);
}

/* 分类 HAL UART 错误，并请求在下一次 read 时重新 arm。
 * ORE 丢失流连续性；FE/NE/PE 只丢失一个坏字节。上下文：UART ISR。 */
void fw_stm32_uart_error(fw_stm32_uart_t *context, UART_HandleTypeDef *uart)
{
    uint32_t errors;
    if (context == NULL || uart != &context->hal || !context->initialized) return;
    errors = uart->ErrorCode;
    ++context->errors;
    if ((errors & HAL_UART_ERROR_ORE) != 0U) {
        /* RX overrun：数据已丢失，parser 必须重新同步。 */
        ++context->rx_overrun_errors;
        context->loss = true;
    } else {
        /* FE/NE/PE（以及保守处理的 DMA）：ring 仍然有效。 */
        ++context->rx_line_errors;
    }
    context->stopped = true;
    /* 如果 HAL 已结束发送，清除在途标记，让下一次 pump 重试该 chunk。
     * 数据仍保留在 ring 中。 */
    if (uart->gState == HAL_UART_STATE_READY) {
        uint32_t mask = lock_tx();
        if (context->tx_active) ++context->tx_errors;
        context->tx_active = false;
        context->tx_inflight = 0U;
        unlock_tx(mask);
    }
}
