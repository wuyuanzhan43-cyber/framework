# Keil AC5 构建入口（STM32F407VET6）

一句话结论：AC5 命令行构建已通过，硬件调试尚待验证。

面向熟悉 Keil 和 STM32 的开发者，本文只记录当前工程的使用方式与已验证边界。
完整目录与协议说明见 [根目录 README](../../README.md)。

当前默认为协议应用模式（`APP_UART_HELLO_TEST=0`）：USART1 PA9/PA10 已启用，
115200 / 8N1，启动 FreeRTOS，每秒主动上报一帧 `CMD=0x90`。
若只想做串口 bring-up，把 `config/app_config.h` 中的 `APP_UART_HELLO_TEST` 改为 `1`，
上电会每秒发送 hello 且不启动 FreeRTOS；接线见 [hello-test.md](../../docs/hello-test.md)。

| 项目 | 当前配置 |
|---|---|
| 工程 | `projects/stm32f407/stm32f407ve.uvprojx` |
| 芯片 | `STM32F407VETx`，Flash 512 KB、主 SRAM 128 KB、CCM 64 KB |
| 编译器 | ARM Compiler 5；本机验证版本为 5.06 update 5，build 528 |
| C 选项 | C99、Debug O0、按函数／数据拆分段 |
| 设备包 | `Keil.STM32F4xx_DFP` |
| 宏 | `STM32F407xx`、`USE_HAL_DRIVER` |
| 启动与 RTOS | ARM 汇编启动文件；FreeRTOS 的 `portable/RVDS/ARM_CM4F` |
| 调试器预设 | CMSIS-DAP；需要改为实际连接的下载器，尚未验证连接 |

## 1. 先准备依赖，再构建

在项目根目录执行：

```text
cmake --preset f407-debug
node tools/build-keil-ac5.mjs
```

第一条命令需要 CMake、Ninja、Git 和 ARM GCC，负责下载并校验固定版本依赖，
同时应用 Keil 启动栈补丁。第二条使用 Node.js 直接调用 AC5 编译、汇编、链接与转换工具，
无须启动 uVision。它读取 `manifest.json` 中的源文件、宏和包含目录；
遇到工具失败或产物为空会报错退出。

默认 AC5 工具目录为当前电脑的 `D:/keil5/stm32/ARM/ARMCC/bin`。
换机器时可指定：

```text
node tools/build-keil-ac5.mjs --arm-dir "D:/YourKeil/ARM/ARMCC/bin"
```

也可设置 `FW_ARMCC_BIN`。默认输出到 `build/keil-ac5/`，保留 `build.log`。
`--release` 选项启用 O2 并输出到 `build/keil-ac5-release/`；本次验证为 Debug。

已有的 PowerShell 构建／生成脚本保留供参考，当前推荐入口为上述 Node 脚本。
维护时以当前清单与真实产物为准。

## 2. 在 uVision 中打开

依赖准备完成后打开 `stm32f407ve.uvprojx`，选择 Debug 目标，确认 AC5 和设备包已安装，
然后执行 Rebuild。工程使用自定义 `stm32f407ve.sct`，输出目录为 `build/keil/`，
与 Node 入口的输出目录分开。

当前完成的是直接调用 AC5 工具的构建，以及工程源码清单核对。
本轮没有完成 uVision GUI 构建验收；非交互调用 UV4 曾停在对话框上，
因此不能用这次命令行成功替代对 GUI 行为的验证。

本机没有可用的 `armclang.exe`，AC6 尚未适配验证。更换编译器时须一起检查
FreeRTOS 移植代码、启动汇编、运行库与链接配置，不能仅修改编译器选项后假定兼容。

## 3. 内存与栈

| 区域 | 起点 | 容量／用途 |
|---|---|---|
| Flash | `0x08000000` | 512 KB，向量表、代码、只读数据和初始化数据 |
| SRAM | `0x20000000` | 128 KB，数据、静态任务与缓冲区、栈 |
| CCM | `0x10000000` | 64 KB，当前未使用 |
| 主／中断栈 | 启动文件中的 STACK 段 | 4 KB |
| C 库堆 | 启动文件中的 HEAP 段 | 保留 512 B；本框架代码未调用 malloc |
| 任务栈 | 项目 main.c 中的静态数组 | 应用 1024 个 32 位字，空闲任务 256 个字 |
| UART 缓冲 | board.c / stm32f4.h 中的静态数组 | RX 512 B + TX ring 512 B + 发送 chunk 64 B + DMA RX 256 B（始终预留） |
| 故障转储 | interrupts.c 中的 `.bss` 结构 | CFSR/HFSR/MMFAR/BFAR、MSP/PSP、异常栈帧 |

Keil 启动文件自己分配 STACK 和 HEAP，并通过 `__initial_sp` 和
`__user_initial_stackheap` 使用它们。分散加载文件将这些区连同其他可写数据放入
有容量上限的 `RW_IRAM1`；超过区域容量会导致链接失败。
这只能检查静态布局，运行时栈溢出仍需要栈检查与实测。

上游启动栈为 1 KB，本工程通过
[`device-arm-startup-stack.patch`](../../third_party/patches/device-arm-startup-stack.patch)
调整到 4 KB。补丁在 CMake 配置依赖时自动应用，Node 构建会确认该配置。
GCC 使用另一份启动汇编，在 GNU 链接脚本中预留 4 KB；
两者区域地址和容量一致，具体段地址与初始 SP 不必相同。

## 4. 当前验证结果

| 检查 | Keil AC5 Debug | ARM GCC Debug |
|---|---|---|
| 命令行构建 | 27 个 C 文件与 1 个启动汇编通过 | CMake 构建通过 |
| Flash 镜像（hello 模式，默认 DMA + LCD + 图片） | 61,868 B | 70,092 B |
| SRAM（hello 模式，默认 DMA + LCD + 图片） | 7,224 B | 6,520 B |
| Flash 镜像（协议模式，默认 DMA + LCD + 图片） | 68,068 B | 78,476 B |
| SRAM（协议模式，默认 DMA + LCD + 图片） | 12,680 B | 12,048 B |
| 回退 IT（LCD 加入前历史数据） | hello 12,860/6,768；协议 19,020/12,216 | hello 18,236/6,064；协议 26,532/11,592 |
| 初始 SP | 以 `__initial_sp` 为准 | `0x20020000` |
| 主要产物 | AXF、HEX、BIN、MAP | ELF、HEX、BIN、MAP |
| 硬件运行 | 2026-10-18 COM16：AC5 DMA 与 GCC DMA 均通过 HIL（含 STATS）及 500 次 PING 压力测试；故障转储/看门狗未触发验证 | 同左 |

协议模式指 `APP_UART_HELLO_TEST 0`，启用 FreeRTOS 与完整协议；相对 hello 模式，
GCC Debug 下 Flash 多约 8.1 KB、SRAM 多约 5.4 KB。
DMA 现在是两条链的默认路径。AC5 回退到 IT 时把 manifest 的
`FW_STM32_UART_DMA` 改为 0；GCC 回退用 `-DFW_UART_DMA=OFF`。
两列均为 2026-10-18 本机实测值（GCC 14.2.1 / ARMCC 5.06 update 5 Debug）。
以上数字不是编译器性能比较。AXF/ELF 含有调试信息，文件大小不等于 Flash 使用量。
协议模式 TX 为 ring + IT/DMA，`write` 不再阻塞等待线路；USART1 DMA 默认开启；IWDG 默认关闭。

UART 现已启用，原先因关闭 UART 产生的不可达代码警告已消失；本次构建无警告或错误。
源码清单与 Keil 工程已核对一致，共 28 个源文件。
Node 入口按函数／数据拆分段，避免把未引用代码全部保留在镜像中。

当前中文工作区内的两条命令行构建均已成功。
曾出现的 HEX 转换失败与传给工具的中文绝对路径有关：
CMake 的 objcopy 和 AC5 脚本现在使用相对输入输出路径。
此结果只覆盖本机工具版本，不能据此保证所有旧版工具或 GUI 都支持任意路径。

## 5. 上板前填写板级信息

[`board_config.h`](../../framework/boards/stm32f407ve/board_config.h) 已配置 LED 为 PE2、高电平点亮，
上电默认低电平。UART 测试配置为 USART1 PA9/PA10，已经启用，需要核对实际接线。
当前内部 HSI 16 MHz 时钟无需外部晶振；若需改为晶振／PLL，继续修改 `board.c`。

当前默认运行协议应用，每秒主动上报一帧 LED 状态，命令码为 `0x90`，无需先发送请求。
若切换到 hello 模式，则只发送文本、不启动 FreeRTOS。
RX/TX 缓冲区各 512 B；TX 使用 ring + IT/DMA，完成中断自动续发。
USART1 DMA 默认开启（manifest `FW_STM32_UART_DMA=1`），已在 COM16 通过 HIL 与压力测试。
独立看门狗默认关闭（`BOARD_IWDG_ENABLED=0`），调试时保持关闭。
上板后运行 `python tests/hil/test_protocol.py --port COMx` 做协议冒烟测试。
工程预设 CMSIS-DAP 只是工程配置，不表示已识别你的下载器。
烧录前应在 Debug 与 Flash Download 中核对实际下载器、芯片及算法。
本次未执行烧录。

## 6. 维护工程

| 改动 | 同步位置 |
|---|---|
| 通用核心、协议或业务源文件 | `manifest.json`（根 CMake 与 Node 构建都从它取清单）、Keil 工程分组 |
| 芯片或板级源文件 | `manifest.json`、Keil 工程分组 |
| 源码目录移动 | `manifest.json`、`.uvprojx` 的 `FilePath`/`IncludePath`、`.uvoptx` 的 `GroupName`、根 CMake；当前分组名已按 `framework/...` 命名 |
| 包含路径、宏、编译选项 | 清单、CMake 与 Keil 工程各自相关配置 |
| Keil 启动栈大小 | 受版本管理的 startup 补丁，而非只改忽略的依赖目录 |

当前 uVision 分组名为：`framework/core`、`framework/components/protocol`、`framework/app`、
`framework/ports`、`framework/boards/stm32f407ve`、`third_party/*`、`project/stm32f407`。
`tools/check-sources.mjs` 比对 `manifest.json` 与 Keil 工程的源文件清单，并已挂进 `ctest`
（测试名 `source_manifest_sync`）。它只检查清单是否一致，不检查编译选项或包含路径。
`tools/gen-keil.ps1` 是原有生成器，其分组和选项也有独立配置；
若使用它重新生成工程，应重新核对清单与选项。不要假设修改任何一份文件都会自动同步全部入口。
修改业务后先跑 Host 测试，涉及目标代码时重新执行对应 ARM 构建。
