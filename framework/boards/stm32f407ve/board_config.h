/*
 * board_config.h - STM32F407VE 引脚、时钟和外设 profile。
 *
 * 层级：board
 * 职责：唯一决定本板使用哪些引脚/实例的地方。
 * 上下文：编译期常量，无运行时状态。
 * 依赖：HAL GPIO/USART/DMA 符号（通过 board.h 引入）。
 *
 * 时钟：内部 HSI 16 MHz，不启用 PLL；确认硬件后再修改。
 * UART 测试接线：USART1 TX=PA9、RX=PA10、AF7；PA9 接适配器 RX，
 * PA10 接适配器 TX，并共地（3.3 V 逻辑）。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* -------------------------------------------------------------------------
 * LED / 指示器
 * ------------------------------------------------------------------------- */
#define BOARD_LED_ENABLED       1
#define BOARD_LED_GPIO          GPIOE
#define BOARD_LED_PIN           GPIO_PIN_2
#define BOARD_LED_ACTIVE_HIGH   1

/* -------------------------------------------------------------------------
 * UART 控制台
 * ------------------------------------------------------------------------- */
#define BOARD_UART_ENABLED      1
/* 当前板级集成支持 USART1、USART2 或 USART3（AF7）。 */
#define BOARD_UART_INSTANCE     USART1
#define BOARD_UART_TX_GPIO      GPIOA
#define BOARD_UART_TX_PIN       GPIO_PIN_9
#define BOARD_UART_RX_GPIO      GPIOA
#define BOARD_UART_RX_PIN       GPIO_PIN_10
#define BOARD_UART_BAUD         115200U
#define BOARD_UART_AF           7U
#define BOARD_UART_IRQ_PRIORITY 6U
#define BOARD_UART_RX_CAPACITY  512U
#define BOARD_UART_TX_CAPACITY  512U

/* -------------------------------------------------------------------------
 * UART DMA（USART1 映射）
 *
 * RX DMA2_Stream2/Ch4，TX DMA2_Stream7/Ch4。两条构建链默认启用 DMA，
 * 已在 COM16 通过 HIL 和 500 次压力测试。回退 IT：
 * GCC 用 -DFW_UART_DMA=OFF；Keil manifest 用 FW_STM32_UART_DMA=0。
 * ------------------------------------------------------------------------- */
#ifndef FW_STM32_UART_DMA
#define FW_STM32_UART_DMA 0
#endif
#define BOARD_UART_DMA_ENABLED      FW_STM32_UART_DMA
#define BOARD_UART_RX_DMA_STREAM    DMA2_Stream2
#define BOARD_UART_RX_DMA_CHANNEL   DMA_CHANNEL_4
#define BOARD_UART_RX_DMA_IRQ       DMA2_Stream2_IRQn
#define BOARD_UART_RX_DMA_IRQ_HANDLER DMA2_Stream2_IRQHandler
#define BOARD_UART_TX_DMA_STREAM    DMA2_Stream7
#define BOARD_UART_TX_DMA_CHANNEL   DMA_CHANNEL_4
#define BOARD_UART_TX_DMA_IRQ       DMA2_Stream7_IRQn
#define BOARD_UART_TX_DMA_IRQ_HANDLER DMA2_Stream7_IRQHandler
#define BOARD_UART_DMA_CLK_ENABLE() __HAL_RCC_DMA2_CLK_ENABLE()

/* -------------------------------------------------------------------------
 * 1.8 寸 128x160 SPI TFT（ST7735S）
 *
 * 模块引脚：GND、VCC、SCL、SDA、RES、DC、CS、BL。
 * 下面是一组可直接使用的 SPI1 接线，可按实际板卡修改：
 *   SCL=PA5  SDA=PA7  CS=PA4  DC=PA6  RES=PA3  BL=PA2
 * SPI1 使用 AF5；PA9/PA10 仍留给 USART1，PE2 仍为 LED。
 * 若画面边缘偏移，调整 X/Y_OFFSET（常见 0/0 或 2/1）；
 * 若颜色反相，调整 BOARD_LCD_INVERT；若红蓝颠倒，调整 MADCTL。
 * ------------------------------------------------------------------------- */
#define BOARD_LCD_ENABLED       1
#define BOARD_LCD_SPI           SPI1
#define BOARD_LCD_SPI_CLK_ENABLE() __HAL_RCC_SPI1_CLK_ENABLE()
#define BOARD_LCD_SCK_GPIO      GPIOA
#define BOARD_LCD_SCK_PIN       GPIO_PIN_5
#define BOARD_LCD_MOSI_GPIO     GPIOA
#define BOARD_LCD_MOSI_PIN      GPIO_PIN_7
#define BOARD_LCD_CS_GPIO       GPIOA
#define BOARD_LCD_CS_PIN        GPIO_PIN_4
#define BOARD_LCD_DC_GPIO       GPIOA
#define BOARD_LCD_DC_PIN        GPIO_PIN_6
#define BOARD_LCD_RES_GPIO      GPIOA
#define BOARD_LCD_RES_PIN       GPIO_PIN_3
#define BOARD_LCD_BL_GPIO       GPIOA
#define BOARD_LCD_BL_PIN        GPIO_PIN_2
/* 最初图片全程亮着时该值为 1；诊断时改 0 导致黑屏，恢复高有效。 */
#define BOARD_LCD_BL_ACTIVE_HIGH 1
#define BOARD_LCD_SPI_AF        5U
#define BOARD_LCD_SPI_PRESCALER SPI_BAUDRATEPRESCALER_2
#define BOARD_LCD_WIDTH         128U
#define BOARD_LCD_HEIGHT        160U
#define BOARD_LCD_X_OFFSET      2U
#define BOARD_LCD_Y_OFFSET      1U
#define BOARD_LCD_MADCTL        0xC0U
/* 实测色条为红→青、绿→紫、蓝→黄、白→黑，因此关闭反相。 */
#define BOARD_LCD_INVERT        0
/* 启动画面选择：0=红绿蓝白色条；1=示例图片；2=纯白诊断。 */
#define BOARD_LCD_BOOT_IMAGE    1
/* 启动图片上是否叠加文字；当前关闭。 */
#define BOARD_LCD_BOOT_TEXT     0

/* -------------------------------------------------------------------------
 * 独立看门狗
 *
 * LSI/32 标称约 1 kHz。使用调试器时保持关闭；核心暂停会停止刷新并复位。
 * ------------------------------------------------------------------------- */
#define BOARD_IWDG_ENABLED      0
#define BOARD_IWDG_TIMEOUT_MS   2000U
#if BOARD_IWDG_ENABLED && (BOARD_IWDG_TIMEOUT_MS < 1U || BOARD_IWDG_TIMEOUT_MS > 4095U)
#error "BOARD_IWDG_TIMEOUT_MS must be 1..4095 for the 12-bit IWDG reload register"
#endif

#endif
