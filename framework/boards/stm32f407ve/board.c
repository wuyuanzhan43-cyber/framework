/*
 * board.c - STM32F407VE 板级装配。
 *
 * 层级：board
 * 职责：配置时钟/引脚，创建设备上下文，并连接 ISR hook。
 * 上下文：init/console 在任务上下文；*_event 函数在 ISR 上下文。
 * 依赖：board.h、board_config.h。
 */
#include "board.h"
#include "board_config.h"

/* 静态存储让整个板级 profile 不使用堆分配。 */
static fw_stm32_indicator_t led;
static fw_stm32_uart_t console;
static uint8_t rx_storage[BOARD_UART_RX_CAPACITY];
static uint8_t tx_storage[BOARD_UART_TX_CAPACITY];
static void (*uart_rx_hook)(void); /* 由项目层安装 */
static void (*uart_tx_hook)(void);

#if BOARD_LCD_ENABLED
static SPI_HandleTypeDef lcd_spi;
static st7735_t lcd;
static bool lcd_initialized;
#endif

/* -------------------------------------------------------------------------
 * 时钟与 GPIO 辅助
 * ------------------------------------------------------------------------- */
/* 只为该 profile 允许使用的端口打开 RCC 时钟。 */
static fw_status_t enable_gpio(GPIO_TypeDef *port)
{
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
    else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
    else if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); }
    else return FW_EINVAL;
    return FW_OK;
}

/* 选择 HSI 16 MHz，不启用 PLL。若修改，必须同步更新 HAL tick。 */
static fw_status_t init_clock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) return FW_EIO;
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_0) != HAL_OK) return FW_EIO;
    SystemCoreClockUpdate();
    return FW_OK;
}

/* 只初始化时钟和可选 LED。串口刻意拆成独立步骤，
 * 这样没有串口的板子仍能通过 board_init()。 */
fw_status_t board_init(void)
{
    GPIO_InitTypeDef pins = {0};
    fw_status_t result = init_clock();
    if (result != FW_OK) return result;
    if (BOARD_LED_ENABLED) {
        if (BOARD_LED_PIN == 0U || enable_gpio(BOARD_LED_GPIO) != FW_OK) return FW_EINVAL;
        led.gpio = BOARD_LED_GPIO;
        led.pin = BOARD_LED_PIN;
        led.active_high = BOARD_LED_ACTIVE_HIGH != 0;
        /* 在改变引脚方向前先建立非激活电平。 */
        HAL_GPIO_WritePin(led.gpio, led.pin, led.active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
        pins.Pin = led.pin;
        pins.Mode = GPIO_MODE_OUTPUT_PP;
        pins.Pull = GPIO_NOPULL;
        pins.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(led.gpio, &pins);
        led.ready = true;
    }
    return FW_OK;
}

/* 配置 UART 引脚、时钟、DMA 映射和 NVIC。成功后幂等。
 * DMA 初始化失败时用中断路径重试，保证板子仍能启动。 */
fw_status_t board_console_init(void)
{
    GPIO_InitTypeDef pins = {0};
    IRQn_Type irq;
    fw_status_t result;
    const fw_stm32_uart_dma_t *dma = NULL;
#if BOARD_UART_DMA_ENABLED
    fw_stm32_uart_dma_t dma_config;
    bool use_dma = false;
#endif
    static bool initialized;
    if (!BOARD_UART_ENABLED) return FW_ENOTREADY;
    if (initialized) return FW_OK;
    if (BOARD_UART_TX_PIN == 0U || BOARD_UART_RX_PIN == 0U ||
        enable_gpio(BOARD_UART_TX_GPIO) != FW_OK ||
        enable_gpio(BOARD_UART_RX_GPIO) != FW_OK) return FW_EINVAL;
    if (BOARD_UART_INSTANCE == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE(); irq = USART1_IRQn;
    } else if (BOARD_UART_INSTANCE == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE(); irq = USART2_IRQn;
    } else if (BOARD_UART_INSTANCE == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE(); irq = USART3_IRQn;
    } else return FW_ENOTSUP;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = BOARD_UART_AF;
    pins.Pin = BOARD_UART_TX_PIN;
    HAL_GPIO_Init(BOARD_UART_TX_GPIO, &pins);
    pins.Pin = BOARD_UART_RX_PIN;
    HAL_GPIO_Init(BOARD_UART_RX_GPIO, &pins);
#if BOARD_UART_DMA_ENABLED
    if (BOARD_UART_INSTANCE == USART1) {
        BOARD_UART_DMA_CLK_ENABLE();
        dma_config.rx_stream = BOARD_UART_RX_DMA_STREAM;
        dma_config.rx_channel = BOARD_UART_RX_DMA_CHANNEL;
        dma_config.tx_stream = BOARD_UART_TX_DMA_STREAM;
        dma_config.tx_channel = BOARD_UART_TX_DMA_CHANNEL;
        dma = &dma_config;
        use_dma = true;
    }
#endif
    result = fw_stm32_uart_init(&console, BOARD_UART_INSTANCE, BOARD_UART_BAUD,
                                rx_storage, sizeof(rx_storage),
                                tx_storage, sizeof(tx_storage), dma);
#if BOARD_UART_DMA_ENABLED
    if (result != FW_OK && use_dma) {
        /* DMA 初始化失败：保留已验证的中断路径。 */
        result = fw_stm32_uart_init(&console, BOARD_UART_INSTANCE, BOARD_UART_BAUD,
                                    rx_storage, sizeof(rx_storage),
                                    tx_storage, sizeof(tx_storage), NULL);
        use_dma = false;
    }
#endif
    if (result != FW_OK) return result;
    HAL_NVIC_SetPriority(irq, BOARD_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(irq);
#if BOARD_UART_DMA_ENABLED
    if (use_dma) {
        HAL_NVIC_SetPriority(BOARD_UART_RX_DMA_IRQ, BOARD_UART_IRQ_PRIORITY, 0U);
        HAL_NVIC_EnableIRQ(BOARD_UART_RX_DMA_IRQ);
        HAL_NVIC_SetPriority(BOARD_UART_TX_DMA_IRQ, BOARD_UART_IRQ_PRIORITY, 0U);
        HAL_NVIC_EnableIRQ(BOARD_UART_TX_DMA_IRQ);
    }
#endif
    initialized = true;
    return FW_OK;
}

/* 板级薄视图：由项目层装配进应用。 */
/* -------------------------------------------------------------------------
 * 1.8 寸 ST7735S TFT
 * ------------------------------------------------------------------------- */
fw_status_t board_lcd_init(void)
{
#if BOARD_LCD_ENABLED
    GPIO_InitTypeDef pins = {0};
    st7735_config_t config;
    fw_status_t result;

    if (lcd_initialized) return FW_OK;
    if (enable_gpio(BOARD_LCD_SCK_GPIO) != FW_OK ||
        enable_gpio(BOARD_LCD_MOSI_GPIO) != FW_OK ||
        enable_gpio(BOARD_LCD_CS_GPIO) != FW_OK ||
        enable_gpio(BOARD_LCD_DC_GPIO) != FW_OK ||
        enable_gpio(BOARD_LCD_RES_GPIO) != FW_OK ||
        enable_gpio(BOARD_LCD_BL_GPIO) != FW_OK) return FW_EINVAL;

    /* SPI1 SCK/MOSI，AF5。 */
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = BOARD_LCD_SPI_AF;
    pins.Pin = BOARD_LCD_SCK_PIN;
    HAL_GPIO_Init(BOARD_LCD_SCK_GPIO, &pins);
    pins.Pin = BOARD_LCD_MOSI_PIN;
    HAL_GPIO_Init(BOARD_LCD_MOSI_GPIO, &pins);

    /* CS/DC/RES/BL 为普通输出；先建立安全电平。 */
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    pins.Alternate = 0U;
    HAL_GPIO_WritePin(BOARD_LCD_CS_GPIO, BOARD_LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_LCD_DC_GPIO, BOARD_LCD_DC_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BOARD_LCD_RES_GPIO, BOARD_LCD_RES_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_LCD_BL_GPIO, BOARD_LCD_BL_PIN, GPIO_PIN_RESET);
    pins.Pin = BOARD_LCD_CS_PIN;
    HAL_GPIO_Init(BOARD_LCD_CS_GPIO, &pins);
    pins.Pin = BOARD_LCD_DC_PIN;
    HAL_GPIO_Init(BOARD_LCD_DC_GPIO, &pins);
    pins.Pin = BOARD_LCD_RES_PIN;
    HAL_GPIO_Init(BOARD_LCD_RES_GPIO, &pins);
    pins.Pin = BOARD_LCD_BL_PIN;
    HAL_GPIO_Init(BOARD_LCD_BL_GPIO, &pins);

    BOARD_LCD_SPI_CLK_ENABLE();
    lcd_spi.Instance = BOARD_LCD_SPI;
    lcd_spi.Init.Mode = SPI_MODE_MASTER;
    lcd_spi.Init.Direction = SPI_DIRECTION_2LINES;
    lcd_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    lcd_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    lcd_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    lcd_spi.Init.NSS = SPI_NSS_SOFT;
    lcd_spi.Init.BaudRatePrescaler = BOARD_LCD_SPI_PRESCALER;
    lcd_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    lcd_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    lcd_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    lcd_spi.Init.CRCPolynomial = 10U;
    if (HAL_SPI_Init(&lcd_spi) != HAL_OK) return FW_EIO;

    config.spi = &lcd_spi;
    config.cs_port = BOARD_LCD_CS_GPIO;
    config.cs_pin = BOARD_LCD_CS_PIN;
    config.dc_port = BOARD_LCD_DC_GPIO;
    config.dc_pin = BOARD_LCD_DC_PIN;
    config.res_port = BOARD_LCD_RES_GPIO;
    config.res_pin = BOARD_LCD_RES_PIN;
    config.bl_port = BOARD_LCD_BL_GPIO;
    config.bl_pin = BOARD_LCD_BL_PIN;
    config.bl_active_high = BOARD_LCD_BL_ACTIVE_HIGH != 0;
    config.width = BOARD_LCD_WIDTH;
    config.height = BOARD_LCD_HEIGHT;
    config.x_offset = BOARD_LCD_X_OFFSET;
    config.y_offset = BOARD_LCD_Y_OFFSET;
    config.madctl = BOARD_LCD_MADCTL;
    config.invert = BOARD_LCD_INVERT != 0;

    result = st7735_init(&lcd, &config);
    if (result != FW_OK) return result;
    lcd_initialized = true;
    return FW_OK;
#else
    return FW_ENOTREADY;
#endif
}

st7735_t *board_lcd(void)
{
#if BOARD_LCD_ENABLED
    return lcd_initialized ? &lcd : NULL;
#else
    return NULL;
#endif
}

void board_lcd_demo(void)
{
#if BOARD_LCD_ENABLED
    if (lcd_initialized) st7735_demo(&lcd);
#endif
}

fw_status_t board_start_console(void) { return fw_stm32_uart_start(&console); }
fw_status_t board_console_pump(void) { return fw_stm32_uart_pump(&console); }

/* 报告编译期 profile 实际包含的能力。 */
board_caps_t board_capabilities(void)
{
    const board_caps_t caps = {
        BOARD_LED_ENABLED != 0,
        BOARD_UART_ENABLED != 0
    };
    return caps;
}

/* LED 被禁用或缺失时返回空设备；应用会映射为状态 3，
 * 而不是假装请求成功。 */
fw_indicator_t board_indicator(void)
{
    const fw_indicator_t none = {NULL, NULL};
    return (BOARD_LED_ENABLED && led.ready) ? fw_stm32_indicator(&led) : none;
}

fw_stream_t board_console(void) { return fw_stm32_uart_stream(&console); }
fw_stm32_uart_t *board_uart_context(void) { return &console; }

/* hook 由 UART/DMA ISR 调用；只能通知/唤醒，不能轮询。 */
void board_set_uart_hooks(void (*rx_event)(void), void (*tx_event)(void))
{
    uart_rx_hook = rx_event;
    uart_tx_hook = tx_event;
}
void board_uart_rx_event(void) { if (uart_rx_hook != NULL) uart_rx_hook(); }
void board_uart_tx_event(void) { if (uart_tx_hook != NULL) uart_tx_hook(); }

/* 用 LSI/32（约 1 kHz）启动 IWDG；禁用时为空操作。 */
void board_watchdog_init(void)
{
#if BOARD_IWDG_ENABLED
    /* LSI/32 约 1 kHz；12 位重装。解锁、配置、启动、刷新。 */
    IWDG->KR = 0x5555U;
    IWDG->PR = 3U;
    IWDG->RLR = BOARD_IWDG_TIMEOUT_MS;
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0xAAAAU;
#endif
}

/* 由主应用循环喂狗。 */
void board_watchdog_refresh(void)
{
#if BOARD_IWDG_ENABLED
    IWDG->KR = 0xAAAAU;
#endif
}
