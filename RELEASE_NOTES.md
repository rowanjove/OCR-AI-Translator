# OCR AI Translator v0.9.0-preview

原生 Windows OCR／AI 翻译与文本处理工具的首个公开预发布包。
产品采用通用名称，Snipaste 是目前实现的 OCR 适配器；后续计划接入其他 OCR 工具并探索内置 OCR。
粘贴翻译独立可用，未来计划不作为本版已支持功能。

## 下载与运行

- 下载 `OCR-AI-Translator-v0.9.0-preview-Win10-x64.zip`。
- 使用同页 `OCR-AI-Translator-v0.9.0-preview-SHA256SUMS.txt` 校验，解压后运行 `Translator.exe`。
- 打开设置 → 模型与 API，填写自己的服务地址、模型名和 API Key，随后粘贴文字即可使用。
- 无需安装器，通常无需管理员权限。当前为 x64／未签名预发布版。
- 中文说明在包内 `USER_GUIDE.zh-CN.md`；源码主页为中文，英文在 `README.en.md`。

## 本版功能

流式翻译、原文编辑／恢复、目标语言选择、五种 AI 改写、自然化表达、三范围自定义提示词、
会话版本回退、多模型 Profile 与翻译／改写角色绑定、深浅主题和工具栏配置。
思考模式强制开关仅适用于代码识别的 DeepSeek 官方端点及 deepseek-flash，其他接口使用自动。

## Snipaste 调用

先安装、授权并启动 Snipaste 专业版桌面程序，在常规设置选择其准确 EXE 路径。
点击“截图翻译”或使用 `Translator.exe --snip`；适配器调用 `snip -o ocr_clipboard`。
建议开启候选确认，核对文字后采用。Esc 取消本工具等待，不结束已有 Snipaste。
也可以先手动 OCR，再用 `Translator.exe --clipboard-text` 载入。
OCR→exec 一步联动仍待验证；本项目不是 Snipaste 官方插件。

## 系统与已知限制

本机已构建／自动化测试环境：Windows 10 x64 build 19045。
Windows 11、真人 IME／物理混合 DPI／多屏、干净机和真实 Provider／OCR 完整矩阵尚未完成验收。
Release 构建 PASS；本版完整 CTest **8 PASS / 1 FAIL / 1 SKIP，98.87 秒**。
`ui_stability_faults` 的严格 GUI 资源零增长断言仍失败，`native_clipboard_isolated` 因本机权限 SKIP。
此前 GitHub Runner 为 9 PASS / 1 FAIL，压力测试在设置预热断言失败。
所有断言均保留，没有把失败改成通过；用户在知悉这些结果后明确批准发布此预览版。
这是预发布版，不宣称稳定版或全面验收完成。

## 隐私、依赖和反馈

包内不含真实 API Key、用户 config.json、DPAPI 密文、私人 OCR／翻译内容、内部日志或测试 EXE。
配置继续沿用 `%LOCALAPPDATA%/SnipasteAITranslator` 兼容路径，以读取既有设置与凭据。
AI 操作会把所选文本发给配置的服务。不要把个人配置、密文或正文附在 Issue 中。
原生 C++20／Win32、静态 CRT，运行时仅引用 Windows 系统 DLL；不带 Electron、Qt、WebView2 或模型。
Apache-2.0；资源／工具链条件见包内 THIRD_PARTY_NOTICES.md。

反馈：[GitHub Issues](https://github.com/rowanjove/OCR-AI-Translator/issues)。

## English

OCR AI Translator is a lightweight native Windows AI translation and text-processing app.
This unsigned Windows x64 preview supports paste translation and the current Snipaste OCR adapter.
More OCR adapters and built-in OCR are plans. Extract the ZIP, run Translator.exe, and configure your own API.
Release build passed; local CTest: 8 PASS / 1 FAIL / 1 SKIP. GUI stress assertions remain failing and disclosed.
The owner explicitly authorized this preview publication. No credentials, private data, test executables or logs
are bundled. See README.en.md for English documentation; report sanitized issues through the link above.
