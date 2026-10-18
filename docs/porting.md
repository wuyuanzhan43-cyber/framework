# 移植指南

本文说明换芯片、换板子或增加一个端口时要改什么，以及怎样确认没有改坏。
当前已验证的参考目标是 STM32F407VET6 + ARM GCC / Keil AC5；Host 端口用于快速回归。

## 1. 先判断属于哪类移植

| 改动 | 需要动的地方 |
|---|---|
| 同一芯片、换引脚/时钟/外设实例 | `framework/boards/<board>/board_config.h`、`board.c` |
| 换 STM32F4 系列芯片 | `framework/boards/`、启动文件、链接脚本、HAL 配置 |
| 换另一家 MCU | 新增 `framework/ports/<mcu>/` 和 `framework/boards/<board>/`，复用 `framework/core/`、`framework/components/`、`framework/app/` |
| 换 RTOS 或裸机 | 新增 `framework/ports/<runtime>/`，提供时钟和休眠适配 |
| 只换业务 | 新增 `framework/app/` 或 `examples/`，不要改 `framework/core/` |

## 2. 板级配置检查表

在 `framework/boards/<board>/board_config.h` 中确认：

- [ ] LED 端口、引脚、有效电平；
- [ ] UART 实例（当前 board 支持 USART1/2/3）、TX/RX 引脚和 AF 编号；
- [ ] 波特率、数据位、停止位、校验；
- [ ] UART IRQ 优先级；
- [ ] RX ring 容量；
- [ ] TX ring 容量（`BOARD_UART_TX_CAPACITY`，默认 512）；
- [ ] 是否启用 LED / UART（`BOARD_LED_ENABLED`、`BOARD_UART_ENABLED`）；
- [ ] 独立看门狗是否启用及超时（`BOARD_IWDG_ENABLED`、`BOARD_IWDG_TIMEOUT_MS`）；
- [ ] DMA 默认开启（GCC `FW_UART_DMA=ON`，AC5 manifest `FW_STM32_UART_DMA=1`）；
      回退用 `FW_UART_DMA=OFF` / manifest 值 0；
- [ ] DMA stream/channel/IRQ 与 USART 实例匹配；当前映射只针对 USART1：
      RX DMA2_Stream2/Ch4，TX DMA2_Stream7/Ch4。

在 `board.c` 中确认：

- [ ] 时钟源和系统频率；若从 HSI 改为 HSE/PLL，必须在改频后重新配置 HAL tick；
- [ ] GPIO 时钟使能；
- [ ] UART 的 NVIC 优先级和使能；
- [ ] 初始输出电平（LED 应先置为熄灭）。

## 3. 端口实现检查表

新增 `fw_stream` 端口时，逐条对照 `framework/include/fw/io.h`：

- [ ] `read` 非阻塞；没有数据返回 `FW_EAGAIN` 且 `count=0`；
- [ ] `read` 绝不写超过 `capacity`；
- [ ] `write` 返回“已接受”的字节数，包括超时/错误时已经接受的字节；
- [ ] 部分写入后调用者只重试后缀；
- [ ] 错误后不得报告“接受了字节但 count=0”；
- [ ] 不保留调用者缓冲区；若使用 TX ring/IT，必须在发送完成前保持内部
      chunk 稳定；
- [ ] 零长度操作成功；
- [ ] 不在 ISR 中调用上层 poll/write；
- [ ] 设备/ops/ctx 生命周期覆盖所有使用期；
- [ ] 若使用 TX 中断/DMA，完成回调负责推进下一段，并保持 accepted 语义；
- [ ] 若使用 RX DMA circular + IDLE，按 HT/TC/IDLE 的位置差值复制，
      不能重复或漏掉跨 wrap 的字节；
- [ ] DMA 初始化失败时可以回退 IT；运行期 DMA 错误要有独立计数；
- [ ] UART 错误回调区分 ORE（必须丢流并复位 parser）与 FE/NE/PE
      （只丢一个坏字节，不清空 ring）。

新增 `fw_indicator` 端口时：

- [ ] `set` 成功只表示“请求已记录”，不承诺光学反馈；
- [ ] 失败时不改变输出状态；
- [ ] 未初始化时返回 `FW_ENOTREADY`。

新增 `fw_clock` 端口时：

- [ ] `now_ms` 单调递增并按 2^32 回绕；
- [ ] 两次读取的间隔必须小于 2^31 ms。

## 4. 目标侧验证顺序

1. **Host 回归**：先跑 `ctest --preset host-debug`，确认业务/协议没有改坏；
2. **编译**：`cmake --preset f407-debug` + `cmake --build --preset f407-debug`；
3. **hello bring-up**：把 `APP_UART_HELLO_TEST` 设为 1，确认 PC 能收到
   `hello\r\n`；接线见 [hello-test.md](hello-test.md)；
4. **协议模式**：改回 0，确认每秒收到 `CMD=0x90` 上报；
5. **HIL**：运行 `python tests/hil/test_protocol.py --port COMx`，覆盖 PING、
   LED/STATUS、未知命令、噪声和分片帧；开启 DMA 后必须重新跑一遍；
6. **DMA**：如需 30 秒持续流量测试，用 HIL 循环发送 PING/STATS，观察
   `io_errors` 和端口计数器；
7. **接收方向**：确认回复和 SEQ 正确，并观察 `g_poll_status`/错误计数；
8. **契约**：把 `tests/conformance` 的适配器接到目标端口，做 loopback
   一致性测试（当前尚未接入，属于下一步工作）；
9. **异常恢复**：人为制造 CRC 错误、噪声、RX 溢出，确认能恢复到新帧；
10. **故障诊断**：触发一次断言/异常（仅测试板），确认 `g_fault_dump` 中有
    CFSR/HFSR 和栈帧；若启用 IWDG，确认系统能复位；
11. **资源**：检查 map 中的 Flash/RAM、任务栈高水位和主栈余量。

## 5. 常见坑

- **HSI 直接跑 UART**：16 MHz HSI 在室温下可用，但高低温或长线场景建议用
  HSE；改 PLL 后不要忘记 `HAL_InitTick()` 或对应的 tick 重新配置。
- **IRQ 优先级**：调用 FreeRTOS API 的 ISR 必须使用数值上不低于
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` 的优先级；当前 UART ISR 不调用
  FreeRTOS API。
- **临界区**：当前 UART 端口用 `PRIMASK` 保护任务侧读取；DMA 模式下重新 arm
  仍在该临界区内，`HAL_UART_AbortReceive` 的耗时需要在目标上测量。
- **环形缓冲区容量**：必须是实际配置值，不要假设 2 的幂；当前实现使用取模，
  不是掩码。
- **半帧超时**：`APP_DEMO_PARTIAL_TIMEOUT_MS` 必须大于一帧的最坏传输时间。
  39 字节在 1200 baud 下约 325 ms，建议至少 400 ms。
- **构建清单**：新增源文件先改 `projects/stm32f407/manifest.json`，再同步
  CMake 与 Keil 工程；`ctest` 中的 `source_manifest_sync` 会检查源文件列表。

## 6. 验收标准

- Host `ctest` 全部通过；
- ARM GCC 构建无警告/错误，Flash/RAM 在预算内；
- 目标板 hello 和协议上报均可见；
- 接收方向 PING/LED_SET/STATUS 均正确；
- 异常注入后能恢复；
- 源码清单、包含路径、宏在 CMake 与 Keil 两处一致（当前自动检查只覆盖源文件）。
