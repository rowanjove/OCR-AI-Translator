# OCR AI Translator

[English](README.en.md) · **v0.9.1** · [下载](https://github.com/rowanjove/OCR-AI-Translator/releases/tag/v0.9.1) · Apache-2.0

一款极轻量的原生 Windows 翻译与文本处理工具，支持自定义 AI API，围绕可扩展的 OCR 来源设计。
直接粘贴文本即可使用；目前已实现 Snipaste OCR 适配器，后续计划接入其他 OCR 工具，并探索内置 OCR。
后续计划尚未实现，不作为当前版本的支持承诺。

## 界面

![浅色主题](docs/screenshots/light.png)
![深色主题](docs/screenshots/dark.png)

截图来自生产版实际窗口，展示深浅主题；未使用合成的翻译结果。

## 功能

- 流式 AI 翻译、目标语言选择、原文解锁编辑、恢复及重新翻译。
- 自然、简洁、口语、正式、文艺五种改写；自然化表达／去 AI 味，不承诺绕过 AI 检测。
- 自定义提示词支持原文+译文、仅译文、仅原文三范围，最多 2000 UTF-16 code units。
- 会话内版本回退，默认保留 10 个完整成功结果，可设置 1–50 个，退出后清除。
- 多 API 配置及翻译／改写角色绑定；思考模式按接口兼容性配置。
- 深色／浅色／跟随系统主题，工具栏顺序、显隐与窄窗更多菜单。
- C++20 + Win32，原生 RichEdit、WinHTTP、DPAPI；没有 Electron、Qt、WebView2 或内置大模型。

最后窗口关闭后本工具退出。用户自己的 OCR 软件或本地模型服务可独立运行。

## 下载与使用

在 [Releases](https://github.com/rowanjove/OCR-AI-Translator/releases) 下载 **OCR-AI-Translator-v0.9.1-Win10-x64.zip**，
用同页 SHA256SUMS 校验后解压到可写目录，运行 `Translator.exe`。通常无需管理员权限。
当前为 **v0.9.1 Stable**，支持 Windows 10 x64，EXE **未签名**。开发者已确认真实 Snipaste OCR 和真实云 Provider 验收通过；本轮自动化结果、最终提交 CI 和校验信息见 [正式发布报告](docs/STABLE_RELEASE_REPORT_2026-10-15.md)。

支持和验收平台：Windows 10 x64 build 19045。Windows 11、物理混合 DPI／多屏、干净机和
真人输入法完整矩阵未验证，不在本轮发布承诺内。未提供 x86、ARM64、Linux 或 macOS 版本。

1. 打开设置 → 模型与 API，填写自己的 HTTPS 服务地址、准确模型 ID 和 API Key。
2. Ctrl+V 载入文字，选择目标语言。启用自动翻译时，采用输入后可立即发送；否则点击“翻译”。
3. 解锁原文后编辑，Ctrl+Enter 提交；改写、自然化、提示词与版本操作位于工具栏。
4. “测试连接”会发送小请求并消耗 API 用量。`--diagnose`／`--provider-check` 默认不联网。

```powershell
.\Translator.exe --clipboard-text
.\Translator.exe --text-file 'C:\Temp\sample.txt'
.\Translator.exe --settings
.\Translator.exe --provider-check translation | Out-String
```

文本文件支持 UTF-8／BOM。卸载时关闭程序并删除解压目录；既有配置保留在
`%LOCALAPPDATA%/SnipasteAITranslator`（沿用旧版兼容路径）。只有明确想删除个人数据时才手动删除它。

## 当前 OCR 适配器：Snipaste

Snipaste 是当前已实现的外部 OCR 来源，需要用户自行安装、授权并启动专业版桌面程序。
在常规设置指定准确 EXE 路径，如 `C:\Tools\Snipaste\Snipaste.exe`；不会搜索 PATH 或回退 WindowsApps。
本项目为独立工具，不是 Snipaste 官方插件。

点击“截图翻译”，或运行：

```powershell
.\Translator.exe --snip
```

适配器调用 `snip -o ocr_clipboard`。建议开启候选确认，核对识别内容后采用；敏感文本可关闭自动翻译。
Esc 取消本工具等待，不结束已有 Snipaste 进程。OCR 会改变剪贴板，而剪贴板变化无法单独证明来源。
真实 Snipaste 专业版 OCR 已由开发者实机确认通过；自动化使用隔离夹具。`--from-snipaste-ocr` 和 OCR→exec 一步联动仍 HOLD。
也可以先自行完成 OCR，再用 `--clipboard-text` 载入。粘贴翻译不依赖任何 OCR 工具。

## AI API 与思考模式

支持 OpenAI Chat Completions 兼容 `/chat/completions`：SSE `delta.content` 和普通 JSON `message.content`。
明确不支持 stream 的响应可回退一次；不保证所有兼容服务都可用。
不支持 Responses API、Anthropic／Gemini 原生协议或视觉 OCR。HTTPS 用于远端，回环 HTTP 可连接已有本地服务；
本工具不安装或启动模型服务。

虚构配置示例：`https://api.example.invalid/v1`、`example-model`、`PLACEHOLDER_NOT_A_KEY`。
`config.example.json` 不含 Key，凭据引用为空；不要覆盖既有用户配置。真实 Key 在本机设置输入并由 DPAPI 加密保存。

每个 Profile 支持自动／关闭／开启思考模式。强制开关仅适配代码识别的 DeepSeek 官方端点与
准确模型名 `deepseek-flash`；其他配置使用自动，自动省略专用参数。
新建且匹配的 DeepSeek Profile 默认关闭思考。详细协议边界见 [API 说明](docs/API_COMPATIBILITY.md)。

## 构建与测试

需要 Visual Studio 2022／Build Tools 的 C++ 桌面开发组件、MSVC x64、Windows SDK 和 CMake ≥ 3.20。
没有需要下载的第三方 C++ 库。PowerShell：

```powershell
git clone https://github.com/rowanjove/OCR-AI-Translator.git
cd OCR-AI-Translator
.\scripts\build.ps1 -BuildDirectory build/local
```

也可在 VS 开发环境分别执行：

```powershell
cmake -S . -B build/local -G 'Visual Studio 17 2022' -A x64
cmake --build build/local --config Release --parallel 2
ctest --test-dir build/local -C Release --output-on-failure
```

输出为 `build/local/Release/Translator.exe`。`/W4 /WX` 将警告视为错误。
完整脚本执行全部 CTest；资源零增长断言保留，任何测试失败都会返回错误。
CI 使用本机夹具和虚构凭据，不调用收费模型。Mock／原生窗口测试不等于真人 OCR／IME／物理 DPI 验收。
具体结果见 [测试说明](docs/TESTING.md) 与 [发布记录](docs/GITHUB_RELEASE_READINESS.md)。

## 隐私与计划

AI 操作会将所选范围内的原文／译文发送给配置的服务商，其保留和收费政策独立。
会话正文和版本默认不落盘，无遥测、更新服务或长期剪贴板监听。
DPAPI 不抵御同用户权限的软件；不要公开配置、DPAPI 密文、私人截图、正文或 HTTP dump。
详见 [安全说明](SECURITY.md)。

后续计划：更多外部 OCR 适配器、内置 OCR 的可行性验证、其他模型协议及完整无障碍／实机兼容性验收。

## 贡献与许可

问题反馈：[GitHub Issues](https://github.com/rowanjove/OCR-AI-Translator/issues)。请附脱敏复现步骤，不附 API Key 或私人内容。
见 [贡献说明](CONTRIBUTING.md)、[架构](docs/ARCHITECTURE.md)、[变更记录](CHANGELOG.md)。
采用 [Apache-2.0](LICENSE)，现有图标已由所有者确认原创授权。
[NOTICE](NOTICE) 和 [资源／依赖说明](docs/THIRD_PARTY_NOTICES.md) 说明相关来源及工具链条件。
