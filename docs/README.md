# docs 目录

| 文档 | 用途 |
|---|---|
| [架构说明](architecture.md) | 分层、依赖红线、并发/错误模型、调度与 DMA |
| [移植指南](porting.md) | 换板、换芯片、新增端口、DMA 与验收检查表 |
| [hello-test.md](hello-test.md) | USART1 bring-up 接线、模式切换与排查 |
| [comment-style.md](comment-style.md) | 文件头、函数契约、字段/宏与测试注释规范 |
| [lcd.md](lcd.md) | 1.8 寸 ST7735S 128×160 SPI TFT 接线、API 与排障 |
| [框架结构详解与使用手册.html](框架结构详解与使用手册.html) | 自包含 HTML 手册：架构、协议、构建、使用、排障 |

Markdown 相对链接由 CTest `docs_links` 检查；HTML 手册不参与编译，可离线打开。
返回根目录 [README](../README.md)。
