# 1.8 寸 128×160 SPI TFT（ST7735S）驱动

## 1. 假设

模块引脚为：

```text
GND  VCC  SCL  SDA  RES  DC  CS  BL
```

这是 **4 线 SPI 串行屏**，不是 RGB 并行接口。1.8 寸 128×160、这组引脚最常见的
控制器是 **ST7735S**；驱动按 ST7735S 初始化序列实现。

如果上电后背光正常但画面全白/全黑，或颜色/方向完全异常，先确认控制器型号；
不同控制器（ST7735R、ST7789、GC9A01 等）不能直接套用本初始化序列。

## 2. 默认接线（可在 board_config.h 修改）

| LCD 模块 | STM32F407VE | 说明 |
|---|---|---|
| GND | GND | 共地 |
| VCC | 3.3 V | 不要接 5 V |
| SCL | PA5 | SPI1 SCK，AF5 |
| SDA | PA7 | SPI1 MOSI，AF5 |
| RES | PA3 | 硬件复位 |
| DC | PA6 | 命令/数据选择 |
| CS | PA4 | 片选，低有效 |
| BL | PA2 | 背光，默认高有效 |

PA9/PA10 仍为 USART1，PE2 仍为 LED，没有占用。

对应配置在 `framework/boards/stm32f407ve/board_config.h`：

```c
#define BOARD_LCD_ENABLED       1
#define BOARD_LCD_SPI           SPI1
#define BOARD_LCD_WIDTH         128U
#define BOARD_LCD_HEIGHT        160U
#define BOARD_LCD_X_OFFSET      2U
#define BOARD_LCD_Y_OFFSET      1U
#define BOARD_LCD_MADCTL        0xC0U
#define BOARD_LCD_INVERT        0
#define BOARD_LCD_BL_ACTIVE_HIGH 1
```

## 3. 代码位置

```text
framework/ports/stm32f4/include/fw/st7735.h   驱动接口
framework/ports/stm32f4/st7735.c              初始化与绘图实现
framework/boards/stm32f407ve/board.c          SPI1/GPIO 装配
```

`main.c` 在 `board_init()` 后调用：

```c
(void)board_lcd_init();
board_lcd_demo();
```

屏幕初始化失败不会阻止串口/协议运行。

## 4. API

```c
st7735_t *lcd = board_lcd();          /* 初始化成功才返回非 NULL */

st7735_fill_screen(lcd, st7735_rgb565(0, 0, 0));
st7735_fill_rect(lcd, 0, 0, 128, 40, st7735_rgb565(255, 0, 0));
st7735_draw_pixel(lcd, 10, 10, st7735_rgb565(0, 255, 0));
st7735_draw_text(lcd, 4, 4, "HANDGLOVE", white, black, 2);
st7735_draw_image(lcd, 0, 0, 128, 160, g_avatar_128x160);
st7735_set_backlight(lcd, false);
st7735_demo(lcd);                     /* 红/绿/蓝/白色条 */
```

颜色为 RGB565：高字节先发送。`st7735_rgb565()` 在头文件内联。

### 文字

- 5×8 ASCII 字体，覆盖 `0x20..0x7E`；
- `scale` 支持 1..4；
- `st7735_draw_text()` 支持 `\n` 和自动换行；
- 字体表在 `framework/ports/stm32f4/st7735_font.h`，只被驱动包含。

### 示例图片

`头像.jpg` 已转换为 128×160 RGB565：

```text
framework/app/include/fw/avatar_128x160.h
framework/app/assets/avatar_128x160.c
```

`main.c` 启动时调用 `st7735_draw_image()` 显示该图。当前
`BOARD_LCD_BOOT_TEXT=0`，不叠加文字；设为 1 可在底部显示 `HANDGLOVE`。
图片数组只读，占用约 40 KB Flash。

## 5. 常见问题

| 现象 | 检查 |
|---|---|
| 背光不亮 | BL 是否高有效、PA2 是否接对、`BOARD_LCD_BL_ACTIVE_HIGH` |
| 全白/全黑 | RES/DC/CS 接线、SPI1 PA5/PA7、控制器是否 ST7735S |
| 画面偏移 | 调整 `BOARD_LCD_X_OFFSET` / `BOARD_LCD_Y_OFFSET`（常见 2/1 或 0/0） |
| 右边/下面有细条花屏 | 多半是可见区偏移不对：先试 2/1；若花屏移到左边/上边，再改回 0/0 |
| 颜色反相/像底片 | 翻转 `BOARD_LCD_INVERT`（1 ↔ 0） |
| 红蓝颠倒、肤色发蓝 | 切换 `BOARD_LCD_MADCTL` 的 BGR 位：`0xC8 ↔ 0xC0` |
| 方向/镜像不对 | 调 `MADCTL` 的 MX/MY 位（0xC0/0x80/0x40/0x00） |
| 方向不对 | 修改 `BOARD_LCD_MADCTL` 的 MX/MY 位 |
| 花屏/闪烁 | 降低 SPI 速度：`BOARD_LCD_SPI_PRESCALER` 改为 `SPI_BAUDRATEPRESCALER_4` |

## 6. 颜色诊断

如果图片颜色异常，先烧录纯色条固件，避免被图片内容干扰：

```text
build/lcd_variants/fw_f407_color_bars_bl_high.hex   # 背光高有效
build/lcd_variants/fw_f407_color_bars_bl_low.hex    # 背光低有效
```

屏幕从上到下应依次为：红、绿、蓝、白。

| 观察结果 | 结论 |
|---|---|
| 四条都很暗/几乎黑 | 背光或 VCC；先试 BL 低有效版本，或把 BL 直接接 3.3 V |
| 白色条很亮，但红/蓝互换 | BGR 位：`MADCTL` 0xC0 ↔ 0xC8 |
| 四条像底片 | 翻转 `BOARD_LCD_INVERT` |
| 颜色顺序正确但图片仍暗 | 图片/格式问题，而不是面板颜色顺序 |
| 色条本身花屏 | SPI 速度/接线问题 |

`BOARD_LCD_BOOT_IMAGE` 可用于在“图片”与“色条”之间切换：

```c
#define BOARD_LCD_BOOT_IMAGE 1   /* 1: 图片；0: 色条；2: 纯白诊断 */
#define BOARD_LCD_BOOT_TEXT  0   /* 1: 在图片底部叠加文字 */
```

## 7. 构建

驱动和 SPI HAL 已加入 `manifest.json`：

```text
framework/ports/stm32f4/st7735.c
third_party/hal-src/Src/stm32f4xx_hal_spi.c
```

GCC/Keil 两条链会自动编译，不需要额外改 CMake。当前 LCD 版本尺寸：

| 配置 | Flash | SRAM |
|---|---|---|
| GCC 协议模式（图片，文字未调用） | 78,476 B | 12,048 B |
| AC5 协议模式（图片，文字未调用） | 68,068 B | 12,680 B |
| GCC hello 模式（图片，文字未调用） | 70,092 B | 6,520 B |
| AC5 hello 模式（图片，文字未调用） | 61,868 B | 7,224 B |

## 8. 当前边界

- 已实现初始化、全屏/矩形填充、画点、5×8 文字、RGB565 图片、背光和色条演示；
- 尚未实现横屏旋转 API、任意字体缩放/中文点阵、图片压缩；
- 驱动使用阻塞 `HAL_SPI_Transmit`，CPU 占用较高但实现简单；
- 需要按实际模块确认控制器、偏移、颜色反相和 RGB/BGR 顺序。
