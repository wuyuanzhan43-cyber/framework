/*
 * fw/st7735.c - ST7735S 128x160 SPI TFT 驱动实现。
 *
 * 层级：port/stm32f4
 * 职责：4 线 SPI 命令/数据发送、窗口设置、矩形填充和色条演示。
 * 上下文：任务上下文；使用阻塞 HAL_SPI_Transmit。
 * 依赖：fw/st7735.h、stm32f4xx_hal.h。
 *
 * 典型模块：GND、VCC、SCL、SDA、RES、DC、CS、BL。
 * 初始化序列基于常见 ST7735S 128x160 模块；若边缘有偏移或颜色反相，
 * 调整 st7735_config_t 的 x_offset/y_offset/invert/madctl。
 */
#include "fw/st7735.h"
#include "st7735_font.h"

#define LCD_CS_LOW(lcd)  HAL_GPIO_WritePin((lcd)->cs_port, (lcd)->cs_pin, GPIO_PIN_RESET)
#define LCD_CS_HIGH(lcd) HAL_GPIO_WritePin((lcd)->cs_port, (lcd)->cs_pin, GPIO_PIN_SET)
#define LCD_DC_CMD(lcd)  HAL_GPIO_WritePin((lcd)->dc_port, (lcd)->dc_pin, GPIO_PIN_RESET)
#define LCD_DC_DATA(lcd) HAL_GPIO_WritePin((lcd)->dc_port, (lcd)->dc_pin, GPIO_PIN_SET)
#define LCD_RES_LOW(lcd) HAL_GPIO_WritePin((lcd)->res_port, (lcd)->res_pin, GPIO_PIN_RESET)
#define LCD_RES_HIGH(lcd) HAL_GPIO_WritePin((lcd)->res_port, (lcd)->res_pin, GPIO_PIN_SET)

#define ST7735_CMD_SWRESET 0x01U
#define ST7735_CMD_SLPOUT  0x11U
#define ST7735_CMD_NORON   0x13U
#define ST7735_CMD_INVOFF  0x20U
#define ST7735_CMD_INVON   0x21U
#define ST7735_CMD_DISPON  0x29U
#define ST7735_CMD_CASET   0x2AU
#define ST7735_CMD_RASET   0x2BU
#define ST7735_CMD_RAMWR   0x2CU
#define ST7735_CMD_MADCTL  0x36U
#define ST7735_CMD_COLMOD  0x3AU

static fw_status_t spi_tx(st7735_t *lcd, const uint8_t *data, uint16_t length)
{
    if (lcd == NULL || lcd->spi == NULL || data == NULL || length == 0U) return FW_EINVAL;
    return HAL_SPI_Transmit(lcd->spi, (uint8_t *)data, length, 100U) == HAL_OK
        ? FW_OK : FW_EIO;
}

static fw_status_t write_cmd(st7735_t *lcd, uint8_t command)
{
    fw_status_t result;
    LCD_CS_LOW(lcd);
    LCD_DC_CMD(lcd);
    result = spi_tx(lcd, &command, 1U);
    LCD_CS_HIGH(lcd);
    return result;
}

static fw_status_t write_cmd_data(st7735_t *lcd, uint8_t command,
                                  const uint8_t *data, uint16_t length)
{
    fw_status_t result;
    LCD_CS_LOW(lcd);
    LCD_DC_CMD(lcd);
    result = spi_tx(lcd, &command, 1U);
    if (result == FW_OK && length != 0U) {
        LCD_DC_DATA(lcd);
        result = spi_tx(lcd, data, length);
    }
    LCD_CS_HIGH(lcd);
    return result;
}

/* 设置绘制窗口，坐标包含面板偏移。 */
static fw_status_t set_window(st7735_t *lcd, uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    fw_status_t result;
    uint16_t xs = (uint16_t)(x0 + lcd->x_offset);
    uint16_t xe = (uint16_t)(x1 + lcd->x_offset);
    uint16_t ys = (uint16_t)(y0 + lcd->y_offset);
    uint16_t ye = (uint16_t)(y1 + lcd->y_offset);

    data[0] = (uint8_t)(xs >> 8U);
    data[1] = (uint8_t)xs;
    data[2] = (uint8_t)(xe >> 8U);
    data[3] = (uint8_t)xe;
    result = write_cmd_data(lcd, ST7735_CMD_CASET, data, 4U);
    if (result != FW_OK) return result;

    data[0] = (uint8_t)(ys >> 8U);
    data[1] = (uint8_t)ys;
    data[2] = (uint8_t)(ye >> 8U);
    data[3] = (uint8_t)ye;
    return write_cmd_data(lcd, ST7735_CMD_RASET, data, 4U);
}

static fw_status_t hardware_reset(st7735_t *lcd)
{
    LCD_CS_HIGH(lcd);
    LCD_RES_HIGH(lcd);
    HAL_Delay(10U);
    LCD_RES_LOW(lcd);
    HAL_Delay(20U);
    LCD_RES_HIGH(lcd);
    HAL_Delay(120U);
    return FW_OK;
}

static fw_status_t init_sequence(st7735_t *lcd)
{
    static const uint8_t frmctr1[] = {0x01U, 0x2CU, 0x2DU};
    static const uint8_t frmctr2[] = {0x01U, 0x2CU, 0x2DU};
    static const uint8_t frmctr3[] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    static const uint8_t invctr[] = {0x07U};
    static const uint8_t pwctr1[] = {0xA2U, 0x02U, 0x84U};
    static const uint8_t pwctr2[] = {0xC5U};
    static const uint8_t pwctr3[] = {0x0AU, 0x00U};
    static const uint8_t pwctr4[] = {0x8AU, 0x2AU};
    static const uint8_t pwctr5[] = {0x8AU, 0xEEU};
    static const uint8_t vmctr1[] = {0x0EU};
    static const uint8_t colmod[] = {0x05U};
    static const uint8_t gmctrp1[] = {
        0x02U, 0x1CU, 0x07U, 0x12U, 0x37U, 0x32U, 0x29U, 0x2DU,
        0x29U, 0x25U, 0x2BU, 0x39U, 0x00U, 0x01U, 0x03U, 0x10U
    };
    static const uint8_t gmctrn1[] = {
        0x03U, 0x1DU, 0x07U, 0x06U, 0x2EU, 0x2CU, 0x29U, 0x2DU,
        0x2EU, 0x2EU, 0x37U, 0x3FU, 0x00U, 0x00U, 0x02U, 0x10U
    };
    uint8_t direction[1];
    fw_status_t result;

    result = write_cmd(lcd, ST7735_CMD_SWRESET);
    if (result != FW_OK) return result;
    HAL_Delay(150U);
    result = write_cmd(lcd, ST7735_CMD_SLPOUT);
    if (result != FW_OK) return result;
    HAL_Delay(120U);

    if ((result = write_cmd_data(lcd, 0xB1U, frmctr1, sizeof(frmctr1))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xB2U, frmctr2, sizeof(frmctr2))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xB3U, frmctr3, sizeof(frmctr3))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xB4U, invctr, sizeof(invctr))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC0U, pwctr1, sizeof(pwctr1))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC1U, pwctr2, sizeof(pwctr2))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC2U, pwctr3, sizeof(pwctr3))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC3U, pwctr4, sizeof(pwctr4))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC4U, pwctr5, sizeof(pwctr5))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xC5U, vmctr1, sizeof(vmctr1))) != FW_OK) return result;

    if (lcd->invert) {
        if ((result = write_cmd(lcd, ST7735_CMD_INVON)) != FW_OK) return result;
    } else {
        if ((result = write_cmd(lcd, ST7735_CMD_INVOFF)) != FW_OK) return result;
    }

    direction[0] = lcd->madctl;
    if ((result = write_cmd_data(lcd, ST7735_CMD_MADCTL, direction, 1U)) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, ST7735_CMD_COLMOD, colmod, sizeof(colmod))) != FW_OK) return result;

    if ((result = write_cmd_data(lcd, 0xE0U, gmctrp1, sizeof(gmctrp1))) != FW_OK) return result;
    if ((result = write_cmd_data(lcd, 0xE1U, gmctrn1, sizeof(gmctrn1))) != FW_OK) return result;
    if ((result = write_cmd(lcd, ST7735_CMD_NORON)) != FW_OK) return result;
    if ((result = write_cmd(lcd, ST7735_CMD_DISPON)) != FW_OK) return result;
    HAL_Delay(100U);
    return FW_OK;
}

fw_status_t st7735_init(st7735_t *lcd, const st7735_config_t *config)
{
    if (lcd == NULL || config == NULL || config->spi == NULL ||
        config->cs_port == NULL || config->dc_port == NULL ||
        config->res_port == NULL || config->width == 0U || config->height == 0U) {
        return FW_EINVAL;
    }
    lcd->spi = config->spi;
    lcd->cs_port = config->cs_port;
    lcd->cs_pin = config->cs_pin;
    lcd->dc_port = config->dc_port;
    lcd->dc_pin = config->dc_pin;
    lcd->res_port = config->res_port;
    lcd->res_pin = config->res_pin;
    lcd->bl_port = config->bl_port;
    lcd->bl_pin = config->bl_pin;
    lcd->bl_active_high = config->bl_active_high;
    lcd->width = config->width;
    lcd->height = config->height;
    lcd->x_offset = config->x_offset;
    lcd->y_offset = config->y_offset;
    lcd->madctl = config->madctl;
    lcd->invert = config->invert;
    lcd->ready = false;

    hardware_reset(lcd);
    if (init_sequence(lcd) != FW_OK) return FW_EIO;
    lcd->ready = true;
    st7735_fill_screen(lcd, 0x0000U);
    st7735_set_backlight(lcd, true);
    return FW_OK;
}

void st7735_set_backlight(st7735_t *lcd, bool on)
{
    if (lcd == NULL || lcd->bl_port == NULL || lcd->bl_pin == 0U) return;
    HAL_GPIO_WritePin(lcd->bl_port, lcd->bl_pin,
        (on == lcd->bl_active_high) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void st7735_fill_rect(st7735_t *lcd, uint16_t x, uint16_t y,
                      uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t row;
    uint16_t column;
    uint16_t line_bytes;
    fw_status_t result;
    uint8_t high = (uint8_t)(color >> 8U);
    uint8_t low = (uint8_t)color;

    if (lcd == NULL || !lcd->ready || width == 0U || height == 0U) return;
    if (x >= lcd->width || y >= lcd->height) return;
    if ((uint32_t)x + width > lcd->width) width = (uint16_t)(lcd->width - x);
    if ((uint32_t)y + height > lcd->height) height = (uint16_t)(lcd->height - y);
    if (width > ST7735_MAX_WIDTH) width = ST7735_MAX_WIDTH;

    if (set_window(lcd, x, y, (uint16_t)(x + width - 1U),
                   (uint16_t)(y + height - 1U)) != FW_OK) return;

    line_bytes = (uint16_t)(width * 2U);
    for (column = 0U; column < width; ++column) {
        lcd->line[column * 2U] = high;
        lcd->line[column * 2U + 1U] = low;
    }

    LCD_CS_LOW(lcd);
    LCD_DC_CMD(lcd);
    if (spi_tx(lcd, (const uint8_t[]){ST7735_CMD_RAMWR}, 1U) != FW_OK) {
        LCD_CS_HIGH(lcd);
        return;
    }
    LCD_DC_DATA(lcd);
    for (row = 0U; row < height; ++row) {
        result = spi_tx(lcd, lcd->line, line_bytes);
        if (result != FW_OK) break;
    }
    LCD_CS_HIGH(lcd);
}

void st7735_fill_screen(st7735_t *lcd, uint16_t color)
{
    if (lcd == NULL) return;
    st7735_fill_rect(lcd, 0U, 0U, lcd->width, lcd->height, color);
}

void st7735_draw_pixel(st7735_t *lcd, uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t data[2];
    if (lcd == NULL || !lcd->ready || x >= lcd->width || y >= lcd->height) return;
    if (set_window(lcd, x, y, x, y) != FW_OK) return;
    data[0] = (uint8_t)(color >> 8U);
    data[1] = (uint8_t)color;
    (void)write_cmd_data(lcd, ST7735_CMD_RAMWR, data, 2U);
}

void st7735_draw_image(st7735_t *lcd, uint16_t x, uint16_t y,
                       uint16_t width, uint16_t height,
                       const uint16_t *pixels)
{
    uint16_t row;
    uint16_t column;
    uint16_t draw_width;
    uint16_t draw_height;
    uint16_t source_stride = width;
    uint16_t line_bytes;

    if (lcd == NULL || !lcd->ready || pixels == NULL || width == 0U || height == 0U) return;
    if (x >= lcd->width || y >= lcd->height) return;

    draw_width = width;
    draw_height = height;
    if ((uint32_t)x + draw_width > lcd->width) draw_width = (uint16_t)(lcd->width - x);
    if ((uint32_t)y + draw_height > lcd->height) draw_height = (uint16_t)(lcd->height - y);
    if (draw_width > ST7735_MAX_WIDTH) draw_width = ST7735_MAX_WIDTH;

    if (set_window(lcd, x, y, (uint16_t)(x + draw_width - 1U),
                   (uint16_t)(y + draw_height - 1U)) != FW_OK) return;

    line_bytes = (uint16_t)(draw_width * 2U);
    LCD_CS_LOW(lcd);
    LCD_DC_CMD(lcd);
    if (spi_tx(lcd, (const uint8_t[]){ST7735_CMD_RAMWR}, 1U) != FW_OK) {
        LCD_CS_HIGH(lcd);
        return;
    }
    LCD_DC_DATA(lcd);
    for (row = 0U; row < draw_height; ++row) {
        const uint16_t *source = &pixels[(uint32_t)row * source_stride];
        for (column = 0U; column < draw_width; ++column) {
            lcd->line[column * 2U] = (uint8_t)(source[column] >> 8U);
            lcd->line[column * 2U + 1U] = (uint8_t)source[column];
        }
        if (spi_tx(lcd, lcd->line, line_bytes) != FW_OK) break;
    }
    LCD_CS_HIGH(lcd);
}

void st7735_draw_char(st7735_t *lcd, uint16_t x, uint16_t y, char c,
                      uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t column;
    uint8_t row;
    uint8_t sx;
    uint8_t sy;
    uint16_t color;

    if (lcd == NULL || !lcd->ready || scale == 0U || scale > 4U) return;
    if (c < 0x20 || c > 0x7E) c = '?';
    glyph = st7735_font5x8[(uint8_t)c - 0x20U];

    /* 5 列字模 + 1 列间距；8 行字模 + 1 行间距。 */
    for (column = 0U; column < 6U; ++column) {
        for (row = 0U; row < 9U; ++row) {
            color = (column < 5U && row < 8U &&
                     (glyph[column] & (uint8_t)(1U << row)) != 0U) ? fg : bg;
            for (sy = 0U; sy < scale; ++sy) {
                for (sx = 0U; sx < scale; ++sx) {
                    st7735_draw_pixel(lcd,
                        (uint16_t)(x + (uint16_t)column * scale + sx),
                        (uint16_t)(y + (uint16_t)row * scale + sy), color);
                }
            }
        }
    }
}

void st7735_draw_text(st7735_t *lcd, uint16_t x, uint16_t y,
                      const char *text, uint16_t fg, uint16_t bg,
                      uint8_t scale)
{
    uint16_t start_x = x;
    uint16_t char_width;
    uint16_t char_height;

    if (lcd == NULL || !lcd->ready || text == NULL || scale == 0U || scale > 4U) return;
    char_width = (uint16_t)(6U * scale);
    char_height = (uint16_t)(9U * scale);

    while (*text != '\0') {
        if (*text == '\n') {
            x = start_x;
            y = (uint16_t)(y + char_height);
            ++text;
            continue;
        }
        if ((uint32_t)x + char_width > lcd->width) {
            x = start_x;
            y = (uint16_t)(y + char_height);
        }
        if (y >= lcd->height) break;
        st7735_draw_char(lcd, x, y, *text, fg, bg, scale);
        x = (uint16_t)(x + char_width);
        ++text;
    }
}

void st7735_demo(st7735_t *lcd)
{
    uint16_t bar;
    if (lcd == NULL || !lcd->ready) return;
    bar = (uint16_t)(lcd->height / 4U);
    st7735_fill_screen(lcd, st7735_rgb565(0U, 0U, 0U));
    st7735_fill_rect(lcd, 0U, 0U, lcd->width, bar, st7735_rgb565(255U, 0U, 0U));
    st7735_fill_rect(lcd, 0U, bar, lcd->width, bar, st7735_rgb565(0U, 255U, 0U));
    st7735_fill_rect(lcd, 0U, (uint16_t)(bar * 2U), lcd->width, bar,
                     st7735_rgb565(0U, 0U, 255U));
    st7735_fill_rect(lcd, 0U, (uint16_t)(bar * 3U), lcd->width,
                     (uint16_t)(lcd->height - bar * 3U),
                     st7735_rgb565(255U, 255U, 255U));
}
