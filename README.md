# STM32F407VE 通用嵌入式框架 · v0.1

一句话结论：业务代码已与芯片解耦，并通过双工具链编译。

**当前默认固件为协议应用模式（`APP_UART_HELLO_TEST=0`）。** USART1 使用 PA9=TX、PA10=RX、
115200 / 8N1，启用 FreeRTOS、命令处理和每秒一次的二进制状态上报。若只想先做串口
bring-up，把 `projects/stm32f407/config/app_config.h` 的 `APP_UART_HELLO_TEST` 改为 `1`，
上电会每秒发送 `hello\r\n`，接线见 [HELLO 测试接线与固件入口](docs/hello-test.md)。
这组引脚是测试配置，板载转串口接线尚未确认。

面向熟悉 STM32、HAL 和 FreeRTOS、希望学习通用框架设计的开发者（基础／进阶）。
当前交付是一条完整的最小业务链：串口字节 → 协议解析 → 命令处理 → LED 输出 → 协议回复。
另支持无需请求的周期状态上报：STM32 应用默认每 1 秒发送一次。LED 已确认为 PE2、高电平点亮。
同一份业务与协议代码可在 PC 模拟环境和 STM32F407VET6 目标中编译。

| 阅读入口 | 用途 |
|---|---|
| [HTML 手册](docs/框架结构详解与使用手册.html) | 结构详解 + 使用手册（自包含、可离线打开） |
| [文档索引](docs/README.md) | docs/ 下架构、移植、hello、HTML 手册的总入口 |
| [framework 目录说明](framework/README.md) | 源码大目录的内部结构与依赖红线 |
| [架构说明](docs/architecture.md) | 分层、依赖红线、并发/错误模型与设计取舍 |
| [注释规范](docs/comment-style.md) | 文件头、函数契约、字段/宏和测试注释的统一结构 |
| [接口约定](framework/include/fw/io.h) | 数据如何传递、谁拥有缓冲区、错误如何表达 |
| [移植指南](docs/porting.md) | 换芯片或换板子要做什么，以及怎么知道做对了 |
| [LCD 驱动](docs/lcd.md) | 1.8 寸 ST7735S 128×160 接线、API 与排障 |
| [示例业务](framework/app/demo_app.c) | 不依赖 ST 或 FreeRTOS 的命令处理 |
| [板级配置](framework/boards/stm32f407ve/board_config.h) | LED 为 PE2、高电平有效；测试口 USART1 PA9/PA10 已启用 |
| [Keil 构建说明](projects/stm32f407/KEIL.md) | AC5 工程与命令行构建 |
| [测试索引](tests/README.md) | Host / conformance / HIL 的分层与运行方式 |
| [工具索引](tools/README.md) | 活跃构建/检查脚本与历史脚本说明 |

## 1. 代码如何分层

| 目录 | 职责 | 依赖边界 |
|---|---|---|
| `framework/app/` | PING、LED_SET、STATUS 与周期主动上报 | 只使用通用接口与协议组件 |
| `framework/app/assets/` | 128×160 RGB565 示例图片 | 只读常量，占用 Flash |
| `framework/components/protocol/` | 组帧、逐字节解析、CRC 校验 | 不包含芯片或操作系统头文件 |
| `framework/core/` | 输入输出实现、状态码、环形缓冲区 | 标准 C 类型，无动态内存分配 |
| `framework/include/fw/` | 公共接口头文件 | 只声明契约，不依赖芯片/OS |
| `framework/ports/stm32f4/` | 将通用接口接到 STM32 HAL | GPIO；UART 中断接收 + TX ring；USART1 DMA 默认开启；SPI1 ST7735S TFT |
| `framework/ports/freertos/` | 时间与任务休眠适配 | 当前要求 1 kHz 系统节拍 |
| `framework/ports/host/` | PC 上的模拟设备与故障注入 | 用于验证业务，不模拟真实电气行为 |
| `framework/boards/stm32f407ve/` | 时钟、引脚、设备实例 | 只在这里决定实际板卡接线 |
| `projects/` | 启动、任务、中断、链接与构建 | 将各层组装成可运行目标 |
| `tests/host/` | 自动验证接口与异常恢复 | 与目标固件共用业务源码 |
| `tests/conformance/` | 17 条接口契约与违规端口 | 只依赖 `fw_core` |
| `tests/hil/` | 真实串口协议冒烟测试 | 需要板卡与 USB-UART，可选 |
| `docs/` | 架构、移植、hello 接线与 HTML 手册 | 不参与编译 |
| `tools/` | 构建、清单与文档检查脚本 | Node.js；历史 PowerShell 脚本见 `tools/README.md` |

调用方向是 `framework/app → 通用接口 → framework/ports → HAL`。这里的 port 是“把统一接口接到具体平台的适配代码”。
`main.c` 负责组装具体对象，业务通过函数表与上下文使用设备，不直接访问 `UART_HandleTypeDef`。
因此，更换芯片时主要增加适配层与板级层，协议和业务可以继续复用。

## 2. 先在电脑运行

需要 CMake ≥ 3.21、Ninja、可在本机运行的 GCC，命令应在项目根目录执行：

```text
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --verbose
```

在支持 ASan/UBSan 的 GCC 环境（Linux 或较新的 MinGW）可改用 `host-asan` 预设；
本机 MinGW 8.1 缺少完整的 `libasan`，CI 使用 Ubuntu 运行该预设。
演示程序位于 `build/host-debug/fw_demo.exe`（Windows），先模拟 1 秒无接收的主动上报，
再发送 LED 开启请求并打印回复。
已验证的示例报文：

```text
autonomous (no request): A5 5A 03 00 90 00 00 00 6F 06
request: A5 5A 01 01 02 01 07 B3
reply:   A5 5A 01 01 82 00 BE B8
LED=ON; accepted requests=1
```

VS Code 的“终端 → 运行任务”提供 `Host: test`、`F407: build (ARM GCC)`、
`F407: build (Keil AC5)`；默认构建任务为 Host。安装 C/C++ 扩展后可选择对应的
IntelliSense 配置，其索引使用 CMake 生成的 `compile_commands.json`。

## 3. 构建 STM32 固件

ARM GCC 路径需要 Git、CMake、Ninja、`arm-none-eabi-gcc` 及其配套工具在 PATH 中：

```text
cmake --preset f407-debug
cmake --build --preset f407-debug
```

首次配置从上游仓库下载 HAL、CMSIS 和 FreeRTOS，按
[`dependencies.lock.json`](third_party/dependencies.lock.json) 校验提交版本，保留原始许可证。
配置阶段还会应用受版本管理的 Keil 启动栈补丁。已有依赖时可用
`cmake --preset f407-debug -DFETCHCONTENT_UPDATES_DISCONNECTED=ON` 避免更新检查。

Keil AC5 可打开 [`stm32f407ve.uvprojx`](projects/stm32f407/stm32f407ve.uvprojx)，
也可在完成上述依赖配置后，通过 Node.js 运行已验证的命令行入口：

```text
node tools/build-keil-ac5.mjs
```

脚本默认工具目录为本机的 `D:/keil5/stm32/ARM/ARMCC/bin`。其他机器通过
`--arm-dir "你的 ARMCC/bin 路径"` 或环境变量 `FW_ARMCC_BIN` 指定目录。
AC5 脚本每次重新编译清单中的源文件；`--release` 选择另一个输出目录并启用 O2，
本次验收使用默认 Debug 配置。AC6 尚未适配验证。

| 构建入口 | 输出目录 | 产物 |
|---|---|---|
| ARM GCC / CMake | `build/f407-debug/projects/stm32f407/` | `fw_f407.elf`、`.hex`、`.bin`、`.map` |
| Keil AC5 / Node | `build/keil-ac5/` | `fw_f407.axf`、`.hex`、`.bin`、`.map`、`build.log` |
| Keil uVision 工程 | `build/keil/` | 工程配置的 AXF、HEX；本轮未完成 GUI 构建验证 |

## 4. 接入真实开发板

`board_config.h` 已启用 PE2 LED，高电平点亮，上电先输出低电平保持熄灭；不自动闪烁。
时钟使用内部 HSI 16 MHz，不启用 PLL。当前选择 USART1 PA9/PA10 作为测试接线并启用 UART。
默认 `APP_UART_HELLO_TEST=0`：启动 FreeRTOS 和二进制协议；改为 1 后只做 hello 周期发送，
不启动 FreeRTOS。请确认电脑串口实际连接这组引脚；若实际板卡使用其他映射，需要修改板级配置。

| 需要确认的硬件信息 | 配置位置与用途 |
|---|---|
| LED（已确认） | PE2，高电平点亮，`BOARD_LED_ENABLED=1` |
| 串口实例、TX/RX 引脚、复用功能 | 测试配置 USART1、PA9/PA10、AF7 已启用；仍需核对实际板卡接线 |
| 1.8 寸 TFT（ST7735S） | SCL=PA5、SDA=PA7、CS=PA4、DC=PA6、RES=PA3、BL=PA2；详见 `docs/lcd.md` |
| 晶振频率与目标系统频率 | 当前可先使用 HSI；若需更改，在 `board.c` 调整时钟与 Flash 等待周期 |
| 下载器类型 | 在 Keil 中选择实际的 ST-Link、CMSIS-DAP 或其他设备 |

默认串口格式为 115200、8 数据位、无校验、1 停止位。
UART 中断优先级为 6，RX/TX 缓冲区各 512 字节。TX 使用 ring：
`write` 只负责入队并返回 accepted，完成中断/DMA 继续泵下一段，不再阻塞 poll 20 ms。
USART1 默认启用 DMA：RX 使用 DMA2_Stream2/Ch4 循环缓冲 + IDLE 事件，
TX 使用 DMA2_Stream7/Ch4。GCC 和 AC5 的 DMA 版本都已在 COM16 上通过
HIL 和 500 次 PING 压力测试（`io_errors=0`、`partial_timeouts=0`）。

### DMA 默认值与回退

GCC 回退到 IT：

```text
cmake --preset f407-debug -DFW_UART_DMA=OFF
cmake --build --preset f407-debug
```

Keil AC5 回退：把 `projects/stm32f407/manifest.json` 的 `defines` 改为
`FW_STM32_UART_DMA=0`，再运行 `node tools/build-keil-ac5.mjs`。

限制：当前 DMA 映射只针对 USART1。只有 DMA 初始化失败时才会自动回退到 IT，
运行期 DMA 故障不会自动切换；需要时用上面的构建开关回到 IT 路径。
`board_init()` 只初始化时钟和 LED；串口由 `board_console_init()` 单独初始化，
无 LED 的板子可以传入空的 indicator，`LED_SET` 会回复状态 3。
独立看门狗默认关闭（`BOARD_IWDG_ENABLED=0`），调试器暂停时不会触发复位；
量产前可打开并确认 `BOARD_IWDG_TIMEOUT_MS`。
所有业务内存和 FreeRTOS 任务均静态分配。
调试时可观察 `g_board_status`、`g_app_status`、`g_app_iterations`、
`g_min_free_stack_words` 和 `g_fault_dump`；栈水位的单位是 32 位栈字，不是字节。

协议帧格式为 `A5 5A LEN SEQ CMD PAYLOAD CRC_LO CRC_HI`。
LEN 仅表示负载长度，最多 32 字节；CRC-16/CCITT-FALSE 覆盖 LEN 到 PAYLOAD，
多项式 `0x1021`、初值 `0xFFFF`、不反射、不异或输出，低字节先发送。

| 请求命令 | 请求负载 | 正常回复负载 |
|---|---|---|
| `0x01` PING | 空 | `00` + ASCII `PONG` |
| `0x02` LED_SET | `00` 关闭或 `01` 开启 | `00` |
| `0x03` STATUS | 空 | `00 known state`，后两字节均为 0/1 |
| `0x04` STATS | 空 | `00 frames(u32 LE) io_errors(u32) partial_timeouts(u32) report_seq(u8)` |

回复沿用 SEQ，CMD 置位 `0x80`。首字节状态码：0 成功、1 参数错误、2 未知命令、
3 输出设备不可用。STATUS 报告最近成功请求的 LED 状态，不能检测 LED 是否实际发光。
首次设置之前 `known=0`。收到回复类型的帧不再回复，避免双方互相应答。

### 主动状态上报

| 项目 | 约定 |
|---|---|
| 触发 | STM32 应用初始化与串口接收启动成功后，无需收到命令 |
| 周期 | 默认 1000 ms；首次在启用后满 1 秒生成，阻塞时可能延后 |
| 命令码 | `0x90`，与 STATUS 回复的 `0x83` 区分 |
| 序号 | 从 0 开始，逐帧递增，255 后回到 0；独立于请求序号 |
| 负载 | `00 known state`，与 STATUS 回复含义相同 |
| LED 行为 | 主动上报本身不改变 LED；上电为低电平，随后通过 LED_SET 控制 |
| 拥塞 | 先补完正在发送的帧，再生成到期的主动帧；不积累错过的周期 |

首次上报示例为 `A5 5A 03 00 90 00 00 00 6F 06`（HEX 显示）。
其中 `known=0` 表示业务尚未接到成功的 LED 设置命令，即使板级初始化已将 PE2 置低。
上报只描述软件记录的输出请求，不检测实际亮度或接线。
上位机识别到 `CMD=0x90` 后直接展示，无须应答。

默认周期定义在 `framework/app/include/fw/demo_app.h` 的 `FW_DEMO_REPORT_PERIOD_MS`。
通用业务初始化默认不主动上报，装配代码调用
`fw_demo_set_report_period(&application, FW_DEMO_REPORT_PERIOD_MS)` 开启。
同一个任务可再次调用该接口调整周期，设为 0 停止后续上报，已开始发送的帧仍会补完。

## 5. 验证结果与边界

| 检查 | 本次结果 |
|---|---|
| Host 自动测试 | CTest 5/5 通过；13 类测试、25,256 次断言检查 |
| 接口一致性 | 三个接口共 17 条契约条款自动验证；另含 5 个故意违规的端口，确认套件能点名到具体条款 |
| 异常场景 | 分包、噪声、CRC 错误、非法长度、部分写入、背压、超时、计时回绕、RX 丢失、实例隔离、缓冲完整帧不被半帧超时误丢、1 ms 上报不饿死接收，以及实现侧越权上报 `count` 时的拒绝 |
| 主动发送 | 无请求上报、周期边界、LED 状态、错过周期合并、停用、序号与计时回绕、部分发送与应答不交错 |
| 命令分发表 | PING/LED_SET/STATUS/STATS 通过表驱动分发；未知命令走统一状态码 2 |
| 事件驱动 | `fw_demo_next_wait_ms` 覆盖上报/半帧超时/TX 挂起/空闲；UART RX/TX/error ISR 通过板级 hook 通知任务 |
| 目标侧 HIL | 2026-10-18 在 COM16 通过全部 8 项：上报、PING、LED/STATUS、未知命令、非法参数、噪声恢复、分片帧、STATS |
| 清单/文档一致性 | `manifest.json` 与 Keil 工程的 28 个源文件逐项一致；Markdown 相对链接由 CTest 自动检查 |
| ARM GCC 14.2.1 Debug（默认 DMA + LCD + 图片） | hello 模式 Flash 70,092 B / SRAM 6,520 B；协议模式 Flash 78,476 B / SRAM 12,048 B |
| ARMCC 5.06 update 5 Debug（默认 DMA + LCD + 图片） | hello 模式 Flash 61,868 B / SRAM 7,224 B；协议模式 Flash 68,068 B / SRAM 12,680 B |
| 加入 LCD 前的回退 IT 数据 | GCC 协议 26,532/11,592；AC5 协议 19,020/12,216 |
| LCD 驱动 + 字体 + 示例图片成本 | 约 48 KB Flash / 456 B SRAM（GCC 协议模式对比） |
| 真实硬件 | 2026-10-18 在 COM16 通过 GCC DMA 与 AC5 DMA 的 HIL（含 STATS）及 500 次 PING 压力测试；故障转储/看门狗尚未触发验证 |

协议模式（`APP_UART_HELLO_TEST 0`）启用 FreeRTOS 与完整协议；默认 DMA 下相对 hello 模式，
GCC Debug 下 Flash 多约 8.1 KB、SRAM 多约 5.4 KB。
相对 IT 路径，DMA 增加约 5.2 KB Flash（GCC）和约 3.5 KB Flash（AC5）。
以上为本机 GCC 14.2.1 / ARMCC 5.06 update 5 Debug 实测值（2026-10-18）。
两种工具链均为主／中断栈预留 4 KB，任务栈另计；Keil 还保留启动文件中的 512 B 库堆。
两条构建链均无编译警告或错误，共享源码按 C99 编译（AC5 没有 C11 模式，故两条链统一到 C99）。

当前输入输出契约是单任务拥有设备，接收非阻塞；目标端口把发送字节复制进 TX ring，
由 IT/DMA 在后台排空，应用只看到 accepted，不再阻塞等待线路。
每次业务轮询默认最多处理 64 个接收字节（`APP_DEMO_RX_BUDGET`，可用
`fw_demo_set_limits()` 在运行时调整）并调用一次发送；待发送帧未完成时先补发剩余部分。
每轮先有界排空 RX，再在发送空闲时处理到期主动帧；回复发送完成后，同一轮仍可补发到期帧，
因此既不会让持续请求饿死上报，也不会让 1 ms 上报周期饿死接收。
主循环不再固定 1 ms 轮询：`fw_demo_next_wait_ms()` 计算最近一次上报/半帧超时，
UART RX/TX/error ISR 通过板级 hook 唤醒任务；空闲时最多每 1 秒兜底唤醒一次。
无流控时持续输入仍可能耗尽有限缓冲区，发生丢失会重置解析器并等待新帧。
未完整接收的帧默认在 250 ms 后丢弃（`APP_DEMO_PARTIAL_TIMEOUT_MS`，低波特率应调大；
仅在整轮没有读到任何字节时才判定超时，ring 中已到达的完整帧不会被误丢）。
HardFault（GCC）会把 CFSR/HFSR/MMFAR/BFAR、MSP/PSP 和异常栈帧记录到
`g_fault_dump`；MemManage/BusFault/UsageFault 记录状态寄存器。独立看门狗默认关闭。
DMA 接收/发送已是默认路径，并通过 GCC/AC5 的 HIL 与压力测试；可用构建开关
回退到 RX IT + TX ring/IT。DMA 映射目前只覆盖 USART1。多任务共享设备、
设备注册表、日志服务、存储和升级仍未实现，这些应在明确需求后逐步扩展。

维护源文件时先更新 `projects/stm32f407/manifest.json`——根目录 CMake 与
`tools/build-keil-ac5.mjs` 都从它取源文件清单；随后同步 Keil 工程的源文件分组，
`ctest` 中的 `source_manifest_sync` 会检查这两处是否仍然一致。
文档改动后 `docs_links` 会检查所有 Markdown 相对链接是否存在。
协议或业务改动先跑 Host 测试，再构建两条 ARM 链路；`host-asan` 预设用于
ASan/UBSan（需要支持 sanitizer 的 GCC，Windows MinGW 8.1 的 libasan 不完整）。
CI 配置在 `.github/workflows/ci.yml`，覆盖 Host、ASan/UBSan 和 ARM GCC；AC5 仍为本地手动验证。
注意：GitHub Actions 只识别**仓库根目录**的 `.github/workflows`；若本项目作为更大仓库的子目录提交，
需要把它单独建仓库，或把 workflow 移到父仓库根。
上板后可用 `python tests/hil/test_protocol.py --port COMx` 做协议冒烟测试，
详见 [tests/hil/README.md](tests/hil/README.md)。
移植学习可以从一个问题开始：如果把串口换成 USB 字节流，哪些文件必须修改，
哪些代码应保持不变？
