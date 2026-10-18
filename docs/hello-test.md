# HELLO 测试接线与固件入口

HELLO 模式用于确认“芯片能跑、时钟正确、串口线接对、PC 能收到字节”。
它只发送文本，不启动 FreeRTOS，也不启用二进制协议接收。

## 1. 切换到 HELLO 模式

编辑 `projects/stm32f407/config/app_config.h`：

```c
#define APP_UART_HELLO_TEST 1
#define APP_UART_HELLO_PERIOD_MS 1000U
```

重新构建并烧录：

```text
cmake --preset f407-debug
cmake --build --preset f407-debug
```

或使用 Keil AC5：

```text
node tools/build-keil-ac5.mjs
```

## 2. 接线

当前板级测试配置（`framework/boards/stm32f407ve/board_config.h`）：

| 信号 | MCU 引脚 | 方向 | 接到 USB-UART |
|---|---|---|---|
| USART1_TX | PA9 | MCU → PC | 适配器 RX |
| USART1_RX | PA10 | PC → MCU | 适配器 TX |
| GND | GND | — | 适配器 GND |

- 电平 3.3 V；
- 串口参数 115200、8 数据位、无校验、1 停止位；
- 这组引脚是**测试配置**，不保证与板载 USB-UART 的接线一致；
- 若实际板卡使用其他映射，修改 `BOARD_UART_*` 后重新核对 AF 编号。

## 3. 预期现象

- 上电后立即发送一次 `hello\r\n`；
- 此后每 `APP_UART_HELLO_PERIOD_MS`（默认 1000 ms）发送一次；
- 不启动 FreeRTOS，不产生 `0x90` 二进制上报；
- LED 保持在熄灭状态（PE2，高电平点亮）。

PC 端可用任意串口终端观察。若完全无输出：

1. 确认 TX/RX 是否交叉、GND 是否共地；
2. 确认 PA9/PA10 没有被其他外设占用；
3. 确认串口参数和端口号；
4. 用示波器/逻辑分析仪看 PA9 是否有波形；
5. 检查 `g_board_status`（`FW_OK=0`）和 `g_hello_*` 计数器。

## 4. 回到协议模式

把 `APP_UART_HELLO_TEST` 改回 `0`，重新构建。协议模式会：

- 启动 FreeRTOS；
- 每 1 秒主动上报 `CMD=0x90`；
- 响应 `PING`、`LED_SET`、`STATUS` 请求。

协议帧格式和命令见根目录 [README](../README.md#4-接入真实开发板)。
