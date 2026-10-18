# 架构说明（v0.1）

本文解释当前实现的边界、依赖规则和关键设计取舍。具体行为以头文件契约和
Host 测试为准；实测数据见根目录 [README](../README.md#5-验证结果与边界)。

## 1. 定位

这是一个**契约驱动的最小闭环**，不是完整框架：

- 一条完整的业务链：UART 字节 → 协议解析 → 命令处理 → LED → 协议回复；
- 同样的业务与协议代码可在 PC（Host 端口）和 STM32F407 目标上编译；
- TX 是非阻塞 ring；USART1 默认使用 DMA（RX circular + IDLE，TX DMA），
  两条工具链均已上板通过 HIL + 压力测试；
- 可用构建开关回退到 RX 逐字节 IT + TX IT；
- 不包含多任务共享设备、设备注册表、日志、存储和升级；
- 所有业务内存和任务栈静态分配，不使用 `malloc`。

框架的价值首先在边界和契约，其次才是功能数量。扩展时应保持这一点。

## 2. 分层与依赖规则

| 目录 | 职责 | 允许依赖 |
|---|---|---|
| `framework/app/` | 示例业务：命令处理、LED 状态、周期上报 | `fw/` 通用接口、协议组件 |
| `framework/components/protocol/` | 组帧、逐字节解析、CRC-16 | 仅标准 C |
| `framework/core/` | `fw_stream`、`fw_indicator`、`fw_clock`、ring、status 的实现 | 仅标准 C |
| `framework/include/fw/` | 公共接口头文件与契约 | 只声明，不依赖芯片/OS |
| `framework/ports/stm32f4/` | 把通用接口接到 STM32 HAL；UART RX 中断 + TX ring/IT，可选 DMA；SPI1 ST7735S TFT | `fw/`、HAL |
| `framework/ports/freertos/` | 时间与任务休眠适配 | FreeRTOS |
| `framework/ports/host/` | PC 模拟设备与故障注入 | `fw/` |
| `framework/boards/` | 时钟、引脚、外设实例和可选能力 | HAL、端口头 |
| `projects/` | 启动、任务、中断、链接、构建 | 以上各层 |

两条红线：

1. `framework/core/` 与 `framework/components/` 不得包含 HAL 或 FreeRTOS 头文件；
2. `framework/app/` 不得包含 `framework/ports/` 头文件，业务只面向 `fw/` 接口。

`framework/ports/stm32f4/include/fw/stm32f4.h` 明确标注为平台私有头，不应从应用或组件引用。

## 3. 数据流

```text
USART RX ISR ──► fw_ring ──► fw_stream_read ──► fw_parser_push
                                                     │
                                               fw_message_t
                                                     │
                                               handle_message
                                        ┌────────────┴────────────┐
                                        ▼                         ▼
                                 fw_indicator_set          tx buffer / encoder
                                                                  │
                                                           fw_stream_write
                                                                  │
                                                            TX ring (port)
                                                                  │
                                              HAL_UART_Transmit_IT / _DMA
                                                                  │
                                                     TxCplt → next chunk
```

Host 端口用内存 ring 和故障注入替代 HAL，因此同一套业务代码可以在 PC 上
验证分包、噪声、CRC 错误、部分写入、背压、超时和回绕。

## 4. 设备抽象与所有权

### 4.1 函数表 + 上下文

每种设备都是“ops 表 + ctx”：

```c
typedef struct {
    const fw_stream_ops_t *ops;
    void *ctx;
} fw_stream_t;
```

应用只看到 `fw_stream_t`，不关心背后是 UART、USB 字节流还是 Host 模拟器。
ops 表中的函数指针和 ctx 的生命周期必须覆盖所有使用期。

### 4.2 缓冲区所有权

- `fw_stream_read`：数据复制到调用者缓冲区；端口不得写入超过 `capacity`；
- `fw_stream_write`：调用者缓冲区在返回后不再被保留；
- 部分写入时，`count` 必须报告真实进展；调用者只重试后缀；
- 错误后不得报告“接受了字节但 count=0”；
- 成功不等于“物理线路发送完成”，只表示端口已接受。

这些规则写在 `framework/include/fw/io.h`，并由 `tests/conformance` 自动验证。

### 4.3 并发模型

- 当前契约是**单任务拥有设备**：`fw_demo_poll` 只能由一个任务调用；
- UART RX 是“ISR 生产、任务消费”：ISR 只向 ring 写 1 字节，任务在
  PRIMASK 临界区内批量读；
- UART TX 是“任务生产、ISR 消费”：`write` 把字节复制进 TX ring 并返回 accepted，
  `HAL_UART_Transmit_IT` 发送当前 chunk，`HAL_UART_TxCpltCallback` 消费已发送
  字节并泵下一段；
- TX ring 的队头和队尾都由 PRIMASK 保护；chunk 在发送完成前不会被覆盖；
- 不允许在 ISR 中调用 `fw_demo_poll` 或 `fw_stream_write`；
- 多任务共享设备、锁和队列不在当前范围内，需要时先补 OSAL 抽象。

### 4.4 可选设备

`fw_demo_init` 只强制要求 stream 和 clock。`indicator.ops == NULL` 表示板子没有
输出设备，`LED_SET` 回复状态 3（输出不可用）。板级通过
`board_capabilities()` 暴露 indicator/console 是否存在，`board_init()` 不再因为
没有串口而整体失败；串口由 `board_console_init()` 单独初始化。

### 4.5 可选 DMA

`FW_UART_DMA=ON`（GCC，默认值）或 manifest 中的 `FW_STM32_UART_DMA=1`（AC5，默认值）启用：

- RX：DMA2_Stream2/Ch4 circular，HAL 的 IDLE/HT/TC 事件把新位置差值复制到 ring；
- TX：DMA2_Stream7/Ch4，每个发送 chunk 完成后继续泵下一段；
- DMA 初始化失败时 board 自动回退到 IT；运行期故障不自动回退。

DMA 路径已在 COM16 上用 GCC 和 AC5 两种构建通过 HIL 及 500 次 PING 压力测试。
回退 IT：GCC `-DFW_UART_DMA=OFF`，AC5 manifest `FW_STM32_UART_DMA=0`。
DMA 缓冲区在 main SRAM（DMA 不可访问 CCM）。

## 5. 调度策略

`fw_demo_poll` 每轮按以下顺序执行：

1. 若有未完成的发送帧，先补发后缀；
2. 有界读取 RX（默认最多 64 字节，可用 `fw_demo_set_limits` 调整）；
3. 若本轮没有任何 RX 字节，且 parser 处于半帧状态并超过
   `partial_timeout_ms`，复位 parser；
4. 若主动上报到期，生成并发送一帧。

这样做的原因：

- **超时不能先于排空**：ring 里可能已经有一帧完整数据，只是应用还没读；
  先复位会把完整帧当噪声丢掉；
- **RX 先获得有界机会**：如果每轮先发上报，1 ms 上报周期会让 RX 永远得不到
  处理；
- **上报仍每轮检查**：连续请求最多占用一轮的有界 RX 预算，不会饿死上报；
- **回复发送完后再补发到期上报**：同一轮可以完成“回复 + 上报”，但不会在
  TX 未完成时覆盖回复缓冲区。

主循环使用 `fw_demo_next_wait_ms()` 计算下一次唤醒时间：

- 有 TX 未完成：1 ms 兜底（正常由 TxCplt 唤醒）；
- 上报到期或半帧超时到期：0，立即 poll；
- 无截止时间：`FW_DEMO_WAIT_FOREVER`，由 UART RX/TX/error ISR 通过
  `board_set_uart_hooks()` 安装的任务通知唤醒；
- 即使空闲也最多每 `APP_EVENT_IDLE_WAIT_MS`（默认 1000 ms）兜底唤醒一次，
  防止漏通知或端口卡死导致任务永久睡眠。

## 6. 内存与错误模型

- 所有对象由调用者静态分配；框架不做堆分配；
- `fw_status_t` 用负数表示错误：`EAGAIN` 是正常背压，不是故障；
- 读错误 `EIO/EOVERFLOW` 表示流连续性丢失，调用者应复位 parser；
- `EOVERFLOW` 表示 RX 缓冲溢出并已丢弃数据；FE/NE/PE 只丢弃一个坏字节，
  不再清空整个 ring；
- 半帧超时只在一轮没有任何 RX 字节时发生，避免误杀已到达的数据；
- 诊断计数器（`frames`、`io_errors`、`partial_timeouts`、端口侧的
  `rx_line_errors`、`rx_overrun_errors`、`tx_errors`）保留在结构中，
  调试器可直接观察；
- `g_fault_dump` 记录 CFSR/HFSR/MMFAR/BFAR 和 MSP/PSP；GCC 的 HardFault
  还会保存异常栈帧（含 faulting PC/LR）。它位于 `.bss`，看门狗复位后会清空；
- 独立看门狗默认关闭，`BOARD_IWDG_ENABLED=1` 后主循环负责刷新。

## 7. 协议

帧格式：

```text
A5 5A LEN SEQ CMD PAYLOAD CRC_LO CRC_HI
```

- `LEN` 仅表示 payload 长度，最大 32；
- CRC 覆盖 `LEN/SEQ/CMD/PAYLOAD`，不覆盖 `A5 5A`；
- CRC-16/CCITT-FALSE：init `0xFFFF`、poly `0x1021`、不反射、不异或；
- 请求命令 bit7=0，回复命令 bit7=1，回复沿用 SEQ；
- 状态码：0 成功、1 参数错误、2 未知命令、3 输出设备不可用；
- 命令由 `command_table[]` 表驱动分发；`PING/LED_SET/STATUS/STATS` 各自是
  独立 handler，新增命令只需增加表项；
- `STATS` 返回 `frames`、`io_errors`、`partial_timeouts` 和 `report_sequence`，
  便于 HIL/现场不接调试器时观察运行状态；
- 主动上报命令 `0x90`，独立序号，周期可配置，错过周期合并。

协议没有版本、地址、ACK 和去重；这些在单设备、短帧、幂等命令场景下不是
问题，但多设备或可靠传输需求出现时应重新设计，而不是继续往当前帧里塞字段。

## 8. 当前明确不做的事

- 其他 USART 实例的 DMA 映射（当前只有 USART1）；
- 多任务共享设备；
- 设备注册表、事件总线、软定时器、日志服务；
- Bootloader、OTA；
- 多芯片/多板 profile；
- AC6 适配。

这些不是“遗漏”，而是当前边界。新增前应先有对应需求、测试和验收标准。

## 9. 扩展顺序建议

1. 用 `tests/hil/test_protocol.py` + 500 次 PING 压力测试做回归；
2. 需要其他 USART 实例时，补对应 DMA stream/channel 映射；
3. 需要多任务时，先引入最小 OSAL（queue/notification/mutex），再谈事件总线；
4. 需要多设备时，再引入实例注册和 capability 查询；
5. 需要产品化时，再补持久化故障记录、协议版本和发布工程。
