# tools 目录

构建、清单检查和文档检查脚本。当前推荐入口全部是 Node.js 脚本。

## 活跃入口

| 脚本 | 用途 | 何时运行 |
|---|---|---|
| `build-keil-ac5.mjs` | 无 GUI 的 Keil AC5 构建，产物在 `build/keil-ac5/` | 修改固件后 |
| `check-sources.mjs` | 比对 `manifest.json` 与 uVision 工程源文件清单 | CTest `source_manifest_sync` |
| `check-docs.mjs` | 检查 Markdown 相对链接 | CTest `docs_links` |

常用命令：

```text
node tools/build-keil-ac5.mjs
node tools/check-sources.mjs
node tools/check-docs.mjs
```

## 历史脚本

以下 PowerShell 脚本保留作参考，当前不作为推荐入口：

- `build-keil-ac5.ps1`：Node 入口之前的 AC5 命令行构建。
- `gen-keil.ps1`：旧的 uVision 工程生成器。

它们通过 `Split-Path -Parent $PSScriptRoot` 定位仓库根；如果要移动到
`tools/legacy/`，必须同时修正根目录计算，否则路径会指向 `tools/`。
在新流程稳定前，建议保持原位并由本 README 标注为历史入口。
