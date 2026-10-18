# HIL 协议冒烟测试

目标：用真实串口验证 STM32 固件的接收方向、协议回复和噪声恢复。
它不是 Host 单元测试的替代，而是目标端口的第一层硬件回归。

> 2026-10-18 已在 `COM16`（115200 8N1）上通过全部 8 项检查（含 `--stats`），
> GCC DMA 与 AC5 DMA 两种构建都验证过；当前默认固件使用 USART1 DMA。

## 准备

1. 固件为协议模式：`APP_UART_HELLO_TEST=0`；
2. 串口接线见 [hello-test.md](../../docs/hello-test.md)（PA9→适配器 RX，PA10←适配器 TX，共地）；
3. 安装依赖：

```text
python -m pip install -r tests/hil/requirements.txt
```

## 运行

```text
python tests/hil/test_protocol.py --port COM7
python tests/hil/test_protocol.py --port /dev/ttyUSB0 --baud 115200
# 当前固件支持 STATS 时追加：
python tests/hil/test_protocol.py --port COM7 --stats
```

脚本会依次验证：

1. 周期主动上报 `0x90`；
2. PING → `PONG`；
3. LED_SET 开/关 + STATUS 状态一致性；
4. 未知命令状态码 2；
5. 非法参数状态码 1；
6. 前导噪声后的帧恢复；
7. 分片帧（先发 3 字节，再发剩余部分）；
8. `--stats` 时额外验证 STATS 计数器回复。

## 当前边界

- 这是协议级 HIL，不是完整的端口契约 conformance；
- 目标端口的 conformance 适配器（把 `tests/conformance` 接到 UART loopback）
  仍是后续工作；
- 脚本默认设备已经启动，若刚复位，等待第一帧最多 `--timeout` 秒。
