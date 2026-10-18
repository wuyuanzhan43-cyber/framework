/*
 * fw/st7735.h - ST7735S 128x160 SPI TFT 驱动。
 *
 * 层级：port/stm32f4
 * 职责：通过 4 线 SPI 初始化 ST7735S，并提供填充、画点和演示色条。
 * 上下文：任务上下文；HAL_SPI_Transmit 为阻塞发送，不在 ISR 中调用。
 * 依赖：stm32f4xx_hal.h、fw/status.h。
 *
 * 模块引脚：GND、VCC、SCL、SDA、RES、DC、CS、BL。
 * 这是 SPI 串行屏，不是 RGB 并行接口。
 */
#ifndef FW_ST7735_H
#define FW_ST7735_H

#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "fw/status.h"

#define ST7735_MAX_WIDTH 160U
#define ST7735_LINE_BYTES (ST7735_MAX_WIDTH * 2U)

typedef struct {
    SPI_HandleTypeDef *spi;      /* 由板级初始化并拥有 */
    GPIO_TypeDef *cs_port;       /* 片选，低有效 */
    uint16_t cs_pin;
    GPIO_TypeDef *dc_port;       /* 命令/数据选择 */
    uint16_t dc_pin;
    GPIO_TypeDef *res_port;      /* 硬件复位 */
    uint16_t res_pin;
    GPIO_TypeDef *bl_port;       /* 背光 */
    uint16_t bl_pin;
    bool bl_active_high;         /* 背光有效电平 */
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;           /* 面板可见区偏移，常见 0/2 */
    uint16_t y_offset;           /* 面板可见区偏移，常见 0/1 */
    uint8_t madctl;              /* 方向 + RGB/BGR，常用 0xC8 */
    bool invert;                 /* 颜色反相；多数 1.8 寸模块需要 */
} st7735_config_t;

typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    GPIO_TypeDef *dc_port;
    uint16_t dc_pin;
    GPIO_TypeDef *res_port;
    uint16_t res_pin;
    GPIO_TypeDef *bl_port;
    uint16_t bl_pin;
    bool bl_active_high;
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    uint8_t madctl;              /* 已应用的方向 + RGB/BGR 设置 */
    bool invert;                 /* 已应用的颜色反相设置 */
    bool ready;
    uint8_t line[ST7735_LINE_BYTES]; /* 填充用的行缓冲，避免逐像素发送 */
} st7735_t;

/* 把 8 位 RGB 转成 RGB565，便于调用者构造颜色。 */
static inline uint16_t st7735_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8U) |
                      ((uint16_t)(g & 0xFCU) << 3U) |
                      ((uint16_t)b >> 3U));
}

/* 初始化面板。SPI 外设和 GPIO 由板级提前配置；本函数负责复位和寄存器序列。
 * 返回 FW_EINVAL 表示参数非法，FW_EIO 表示 SPI 发送失败。 */
fw_status_t st7735_init(st7735_t *lcd, const st7735_config_t *config);

/* 控制背光；on 按 bl_active_high 映射到 GPIO。 */
void st7735_set_backlight(st7735_t *lcd, bool on);

/* 用同一颜色填充整个屏幕。 */
void st7735_fill_screen(st7735_t *lcd, uint16_t color);

/* 填充矩形；坐标会被裁剪到屏幕范围，宽高为 0 时无操作。 */
void st7735_fill_rect(st7735_t *lcd, uint16_t x, uint16_t y,
                      uint16_t width, uint16_t height, uint16_t color);

/* 画一个像素；越界时无操作。 */
void st7735_draw_pixel(st7735_t *lcd, uint16_t x, uint16_t y, uint16_t color);

/* 绘制 RGB565 图片。像素按行存储，高字节先发；超出屏幕的部分会被裁剪。 */
void st7735_draw_image(st7735_t *lcd, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height,
                       const uint16_t *pixels);

/* 绘制一个 ASCII 字符（0x20..0x7E），5x8 字体，scale=1..4。
 * 背景色会被写入字模区域；超出屏幕的部分会自动裁剪。 */
void st7735_draw_char(st7735_t *lcd, uint16_t x, uint16_t y, char c,
                      uint16_t fg, uint16_t bg, uint8_t scale);

/* 绘制字符串；支持 '\n'。x 是起始列，自动按字符宽度换行。 */
void st7735_draw_text(st7735_t *lcd, uint16_t x, uint16_t y,
                      const char *text, uint16_t fg, uint16_t bg,
                      uint8_t scale);

/* 画红/绿/蓝/白色条，用于快速确认接线、方向和颜色顺序。 */
void st7735_demo(st7735_t *lcd);

#endif
