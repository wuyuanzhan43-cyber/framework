/*
 * fw/avatar_128x160.h - 128x160 RGB565 示例图片。
 *
 * 层级：app/assets
 * 职责：为 ST7735S 屏幕提供一张 128x160 的启动图片。
 * 上下文：只读常量，放在 Flash 中。
 * 依赖：stdint.h。
 */
#ifndef FW_AVATAR_128X160_H
#define FW_AVATAR_128X160_H
#include <stdint.h>
#define AVATAR_WIDTH 128U
#define AVATAR_HEIGHT 160U
extern const uint16_t g_avatar_128x160[AVATAR_WIDTH * AVATAR_HEIGHT];
#endif
