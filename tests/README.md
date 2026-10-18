# tests 目录

验证分为三层：Host 单测 → 接口 conformance → 真实硬件 HIL。

| 目录 | 内容 | 运行方式 |
|---|---|---|
| `host/` | 13 组 Host 测试：ring、协议、业务、背压、回退、上报、契约 | `ctest --preset host-debug` |
| `conformance/` | 17 条接口契约 + 5 个故意违规端口 | CTest `interface_conformance` |
| `hil/` | 真实串口协议冒烟测试 | `python tests/hil/test_protocol.py --port COMx --stats` |

## 推荐验证顺序

1. Host 构建与 CTest：

```text
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure
```

2. 构建并烧录目标固件（详见 [README](../README.md)）。

3. HIL：

```text
python tests/hil/test_protocol.py --port COM16 --stats
```

4. DMA 或协议改动后，建议追加连续请求压力测试，观察 `STATS` 的
   `io_errors` 和 `partial_timeouts` 是否保持为 0。

## 当前覆盖边界

- Host 侧 17 条 conformance 已通过；
- 目标侧完整 conformance（loopback + 故障注入）尚未实现，已按需求跳过；
- HIL 覆盖正常协议行为和 DMA 路径，不替代目标端口契约认证；
- 故障转储/看门狗只有代码与编译验证，尚未做故障注入实测。
