/*
 * main.c - STM32F407 目标的项目装配。
 *
 * 层级：project
 * 职责：拥有应用实例、任务、ISR 到任务的转交以及模式选择。
 * 上下文：main/应用任务；uart_isr_event 由 UART/DMA ISR 调用。
 * 依赖：FreeRTOS、board.h、fw/demo_app.h、app_config.h。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "board_config.h"
#include "fw/demo_app.h"
#include "fw/freertos_runtime.h"
#include "fw/avatar_128x160.h"
#include "app_config.h"

/* 启动诊断：LCD 初始化结果，可在调试器观察。 */
volatile fw_status_t g_lcd_status = FW_ENOTREADY;
volatile bool g_lcd_ready;

#if !APP_UART_HELLO_TEST
static fw_demo_app_t application;
static StaticTask_t application_control;
static StackType_t application_stack[1024]; /* 32 位字，不是字节 */
static TaskHandle_t application_task_handle;

/* 由 UART RX/TX/error ISR 调用。优先级 6 在数值上低于
 * configMAX_SYSCALL_INTERRUPT_PRIORITY（5），因此可安全调用
 * FreeRTOS FromISR API。 */
/* 最小 ISR 侧转交：唤醒拥有任务，绝不在 ISR 中轮询。 */
static void uart_isr_event(void)
{
    BaseType_t woken = pdFALSE;
    if (application_task_handle != NULL)
        vTaskNotifyGiveFromISR(application_task_handle, &woken);
    portYIELD_FROM_ISR(woken);
}
#endif
/* -------------------------------------------------------------------------
 * 调试器可见状态
 *
 * g_board_status 和 g_app_status 是初始化结果：setup 期间写一次，之后
 * 不再修改。非 0 始终表示“初始化失败”。运行期健康度放在下面的计数器中，
 * 这样一次瞬时 RX 溢出不会被误判为应用死亡。
 * ------------------------------------------------------------------------- */
volatile fw_status_t g_board_status = FW_ENOTREADY;
volatile fw_status_t g_app_status = FW_ENOTREADY;
volatile fw_status_t g_poll_status = FW_ENOTREADY;
volatile uint32_t g_poll_error_count;
volatile uint32_t g_app_iterations;
volatile uint32_t g_min_free_stack_words;
volatile uint32_t g_hello_tx_calls;
volatile uint32_t g_hello_bytes_accepted;
volatile uint32_t g_hello_lines_accepted;
volatile uint32_t g_hello_errors;
volatile fw_status_t g_hello_last_status = FW_ENOTREADY;
const char * volatile g_assert_file;
volatile unsigned g_assert_line;

/* 最后兜底的断言 hook：记录源位置并在关中断后停机。
 * 供 configASSERT 和 HAL assert_param 使用。 */
void fw_panic(const char *file, unsigned line)
{
    __disable_irq();
    g_assert_file = file;
    g_assert_line = line;
    for (;;) { __NOP(); }
}

/* -------------------------------------------------------------------------
 * Bring-up 模式
 * ------------------------------------------------------------------------- */
#if APP_UART_HELLO_TEST
/* 裸机 UART 检查：不启动调度器，也不启用协议 parser。 */
static void hello_test(void)
{
    static const uint8_t text[] = "hello\r\n";
    fw_stream_t stream;
    size_t offset = 0U;
    g_app_status = g_board_status;
    if (g_app_status == FW_OK) g_app_status = board_console_init();
    if (g_app_status != FW_OK) fw_panic(__FILE__, __LINE__);
    board_watchdog_init();
    stream = board_console();
    for (;;) {
        size_t accepted = 0U;
        board_watchdog_refresh();
        fw_status_t result = fw_stream_write(&stream, text + offset,
            sizeof(text) - 1U - offset, &accepted, 100U);
        g_hello_last_status = result;
        ++g_hello_tx_calls;
        g_hello_bytes_accepted += (uint32_t)accepted;
        (void)board_console_pump();
        /* EAGAIN 是普通背压。其他返回值是真实驱动故障；否则这里会不可见：
         * 即使没有数据到达 PC（引脚或波特率错误），accepted 计数看起来仍健康，
         * 因为 UART 发送器无法知道输出是否被观察。 */
        if (result != FW_OK && result != FW_EAGAIN) ++g_hello_errors;
        offset += accepted;
        if (offset == sizeof(text) - 1U) {
            ++g_hello_lines_accepted;
            offset = 0U;
            HAL_Delay(APP_UART_HELLO_PERIOD_MS);
        } else {
            /* 保留部分进展；不要重复发送已被接受的字节前缀。 */
            HAL_Delay(10U);
        }
    }
}
#else
/* -------------------------------------------------------------------------
 * 协议应用
 * ------------------------------------------------------------------------- */
/* 从板级设备装配应用，然后以事件驱动循环 poll/wait。
 * 初始化失败时 g_app_status 保持非 0，并让任务停驻而不空转。 */
static void application_task(void *argument)
{
    (void)argument;
    application_task_handle = xTaskGetCurrentTaskHandle();
    if (g_board_status == FW_OK) {
        g_app_status = fw_demo_init(&application, board_console(), board_indicator(),
                                    fw_freertos_clock());
        if (g_app_status == FW_OK)
            g_app_status = fw_demo_set_limits(&application, APP_DEMO_PARTIAL_TIMEOUT_MS,
                                              APP_DEMO_TX_TIMEOUT_MS, APP_DEMO_RX_BUDGET);
        if (g_app_status == FW_OK) g_app_status = board_console_init();
        if (g_app_status == FW_OK) board_set_uart_hooks(uart_isr_event, uart_isr_event);
        if (g_app_status == FW_OK) g_app_status = board_start_console();
        if (g_app_status == FW_OK)
            g_app_status = fw_demo_set_report_period(&application, FW_DEMO_REPORT_PERIOD_MS);
    }
    if (g_app_status != FW_OK) {
        /* 初始化失败：g_app_status 保持非 0，标识失败的步骤。
         * 保持板级 LED 的非激活电平。 */
        for (;;) fw_freertos_sleep_ms(1000U);
    }
    board_watchdog_init();
    for (;;) {
        fw_status_t result = fw_demo_poll(&application);
        (void)board_console_pump();
        board_watchdog_refresh();
        g_poll_status = result;
        /* EAGAIN 是普通 TX 背压，不是故障。这里的 EOVERFLOW 表示 RX 字节突发
         * 超过 512 字节 ring，连续性已丢失。 */
        if (result != FW_OK && result != FW_EAGAIN) ++g_poll_error_count;
        ++g_app_iterations;
        g_min_free_stack_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        {
            uint32_t wait = fw_demo_next_wait_ms(&application);
            TickType_t ticks = (wait == FW_DEMO_WAIT_FOREVER)
                ? pdMS_TO_TICKS(APP_EVENT_IDLE_WAIT_MS)
                : pdMS_TO_TICKS(wait);
            (void)ulTaskNotifyTake(pdTRUE, ticks);
        }
    }
}
#endif

/* FreeRTOS 静态分配 hook：空闲任务使用这块存储。 */
void vApplicationGetIdleTaskMemory(StaticTask_t **control, StackType_t **stack,
                                    uint32_t *size)
{
    static StaticTask_t idle_control;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *control = &idle_control;
    *stack = idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

/* FreeRTOS 栈溢出 hook：记录任务后停机。 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    fw_panic(__FILE__, __LINE__);
}

/* 组合根：HAL -> 板级 profile -> 模式入口。 */
int main(void)
{
    HAL_Init();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    g_board_status = board_init();
    if (g_board_status == FW_OK) {
        st7735_t *lcd;
        g_lcd_status = board_lcd_init();
        g_lcd_ready = board_lcd() != NULL;
        lcd = board_lcd();
        if (lcd != NULL) {
#if BOARD_LCD_BOOT_IMAGE == 1
            st7735_draw_image(lcd, 0U, 0U, AVATAR_WIDTH, AVATAR_HEIGHT,
                              g_avatar_128x160);
#if BOARD_LCD_BOOT_TEXT
            st7735_draw_text(lcd, 4U, 140U, "HANDGLOVE",
                             st7735_rgb565(255U, 255U, 255U),
                             st7735_rgb565(0U, 0U, 0U), 2U);
#endif
#elif BOARD_LCD_BOOT_IMAGE == 0
            st7735_demo(lcd);
#else
            /* 纯白诊断：白屏若也变黑，优先查 VCC/BL/背光供电。 */
            st7735_fill_screen(lcd, st7735_rgb565(255U, 255U, 255U));
            st7735_draw_text(lcd, 4U, 4U, "WHITE",
                             st7735_rgb565(0U, 0U, 0U),
                             st7735_rgb565(255U, 255U, 255U), 2U);
#endif
        } else {
            board_lcd_demo();
        }
    }
#if APP_UART_HELLO_TEST
    hello_test();
#else
    configASSERT(xTaskCreateStatic(application_task, "demo", 1024U, NULL, 2U,
                                   application_stack, &application_control) != NULL);
    vTaskStartScheduler();
    fw_panic(__FILE__, __LINE__);
#endif
    return 0;
}
