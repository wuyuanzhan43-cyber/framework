# framework 目录

所有框架源码集中在这里；`projects/`、`tests/`、`docs/`、`tools/` 留在仓库外层。

```text
framework/
├─ app/                 示例业务：命令表、STATS、LED、上报
├─ components/protocol/ 帧编码、parser、CRC
├─ core/                io / ring / status 的实现
├─ include/fw/          公共接口头文件与契约
├─ ports/               host / freertos / stm32f4 适配（UART、GPIO、SPI TFT）
└─ boards/              stm32f407ve 板级配置
```

## 依赖方向

```text
framework/app → framework/components → framework/core
                                         ▲
                              framework/ports
                                         ▲
                                  HAL / FreeRTOS
```

`framework/boards/` 决定具体板卡引脚、时钟和外设实例；`projects/` 负责把它们装配成可运行目标。

## 红线

1. `framework/core/` 与 `framework/components/` 不得包含 HAL 或 FreeRTOS 头文件；
2. `framework/app/` 不得包含 `framework/ports/` 头文件；
3. 所有业务与框架内存静态分配，不使用 `malloc`；
4. 设备由单任务拥有；ISR 不调用上层 poll/write。

## 入口

- [架构说明](../docs/architecture.md)
- [移植指南](../docs/porting.md)
- [HTML 手册](../docs/框架结构详解与使用手册.html)
- [根 README](../README.md)
