# Changelog

Changes to OCR AI Translator. Historical entries describe their original
stage; current implementation and validation are in release readiness.

## [0.9.1-preview] - 2026-10-15

### Fixed
- Saving Provider settings and using paste translation no longer depends on the optional Snipaste executable being installed; OCR startup still validates its configured executable.
- New configurations and missing preference fields now default to automatic translation off and OCR candidate confirmation on. Explicit values in existing configurations remain unchanged.
- Fixed the settings warmup failure in the GUI stability test by using a deterministic missing OCR path and explicitly warming the first settings open/theme preview after final quiescence. Strict measured resource assertions remain unchanged.

### Validation
- Windows 10 x64 Release build passed; full CTest: 9 PASS / 0 FAIL / 1 SKIP. The clipboard-isolation test skipped because this host could not create a private window station (Win32 error 5).
- `ui_stability_faults` passed three consecutive runs and in the full suite. First-use settings warmup can add one GDI object; its source remains unidentified, while the following 50 settings cycles and 100 render cycles show no growth.
- Windows 11, clean-machine, live Provider, real Snipaste OCR and physical IME/DPI coverage remain separately unverified.
- This is a preview candidate, not a Stable release.

## [0.9.0-preview] - 2026-10-14

### Public preview update
- Renamed public product/repository to OCR AI Translator / OCR-AI-Translator.
- Chinese README is the default homepage; English moved to README.en.md.
- Existing configuration/DPAPI/IPC identifiers preserved for compatibility.
- Owner authorized publishing the preview ZIP with known automated failures disclosed.

### Added
- Bilingual public README, current scope/architecture/API/testing documentation,
  contribution and security guidance, safe configuration example.
- Owner-selected Apache-2.0 license and original-icon authorization record.
- Opt-in public Git inventory, staged content audit and Windows x64 CI.
- CMake/PE/About version 0.9.0-preview and portable candidate packaging/checksums.

### Changed
- Release description reflects paste translation as an independent core feature
  and Snipaste as the sole implemented, still-unverified OCR adapter.
- Public source excludes historical private evidence, logs, test credentials,
  caches, old packages and internal agent handoffs without deleting originals.

### Current inherited implementation
- Native two-pane UI, source editing, multiple API profiles, role binding, memory
  versions, five rewrite presets, naturalization and scoped custom instructions.
- Thinking mode adapter for the recognized official DeepSeek endpoint/model.
- Capture session isolation, hidden-window lifecycle, configuration rollback and
  failed-request/source-language restoration hardened in preceding local work.

### Known limitations
- Strict GUI resource-stability failure remains unresolved; Stable NO-GO.
- Real OCR, final-binary live Provider, human IME, physical DPI/multi-monitor,
  Windows 11 and clean-machine acceptance UNVERIFIED.
- Unsigned executable; first hosted CI run and MSVC binary redistribution review
  pending. Preview candidate preparation is not a public release.

## [0.2.0-preview.p1] - 2026-10-13

### Added
- **P1-A1 统一 AI 操作抽象**：
  - 新增 `AiOperationType` 枚举（`translate`, `rewrite`, `naturalize`, `custom`），统一复用 WinHTTP/SSE 流式引擎。
  - 请求与事件结构扩展操作类型、模型配置 ID、会话及父版本 ID，支持并发事件精确路由与代次隔离。
  - 改写或自定义操作出错/取消时自动恢复上一完成译文，保障文本不丢失。
- **P1-A2 AI 改写与去 AI 味**：
  - 新增 5 种预设改写风格：自然 (natural)、简洁 (concise)、口语 (casual)、正式 (formal)、文艺 (literary)。
  - 新增“去 AI 味”自然化功能，专用系统提示词消除翻译腔与模板化套话。
  - 提示词构建采用结构化数据区隔离（`[Original Source Text]` 与 `[Current Translation]`），区分指令与数据；不保证模型抵抗提示词注入。
- **P1-A3 自定义提示词浮动面板**：
  - 窗口内浮动面板 `SAT.CustomPrompt`，支持三种作用范围：原文+译文、仅当前译文、仅当前原文。
  - 输入字符上限 2000 字符限制与校验提示。
  - 会话内记忆最近 5 条历史提示词，提供快速下拉复选，退出即焚不落盘。
  - 交互快捷键：`Ctrl+Enter` 执行，`Esc` 快速撤回并关闭浮层。
- **P1-A4 会话内译文版本管理**：
  - 内存跟踪最近 10 个完整成功译文版本（`TextVersion`），超限 FIFO 自动清理最旧版本。
  - 仅完整成功结果加入历史；支持版本列表查看与一键版本回退。
  - 跨原文版本回退时同步恢复对应原文快照，防止语义漂移。退出即清除，零磁盘驻留。
- **P1-A5 多 Provider Profile 与模型角色分工**：
  - 支持多套模型服务商配置（Base URL、模型、API Key、超时、最大 Token），支持添加与删除。
  - 模型角色分工：可分别为“翻译”和“改写/自定义”分配不同的 Profile。
  - 完美向后兼容旧单模型配置迁移。
  - Windows DPAPI 加密凭据生命周期升级，支持多 Profile 孤儿凭据自动清理。
- **P1-B1 工具栏与设置交互完善**：
  - 底部工具栏布局升级为双行紧凑排列，包含 4 个新功能按钮（`✦ 改写`、`⚡ 去 AI 味`、`✎ 提示词`、`🕒 版本`）。
  - 设置窗口增加译文栏工具按钮显隐复选框与多 Profile 管理控件，配置持久化。
- **P1-B2 & B3 自动化回归测试与便携产物**：
  - 完成回归矩阵 P1-T01 至 P1-T15，覆盖风格模板、结构化输入边界、作用范围、版本淘汰、Profile 迁移与 DPAPI 清理。
  - 单一便携版二进制 `Translator.exe`（620,032 bytes，~605.5 KiB）。

### Changed
- 修复 `Config::syncProfilesFromActive()` 在多配置保存时的凭据覆盖问题，确保 profile 自身凭据与有效 ref 优先。
- 升级 `cleanupOrphanCredentials()` 接收多配置活跃凭据列表，避免删除有效 profile 凭据。
- 改进 `customPromptProc` 与 `layoutMain` 的子窗口可见性检测，兼容测试模式下隐藏窗口运行。

### Fixed
- 修复 `translation_client.hpp` 与 `translation_client.cpp` 中的 `chatBody` 作用域与 ADL 二义性。
- 修复 `tests/core_tests.cpp` 缺失 `<deque>` 头文件的问题。

### Boundaries / Known Limitations (HOLD)
- 真实物理机 Snipaste 专业版 OCR GUI 外部调用闭环保持 **HOLD / NO-GO**。
- 真实公网 Provider HTTPS API 联调保持 **UNVERIFIED / HOLD**。
- 真实真人物理中文输入法与跨显示器 DPI 物理交互保持 **UNVERIFIED / HOLD**。

---

## [0.1.0-preview.p0] - 2026-10-12

### Added
- P0 基础双栏翻译窗口（原文/译文 RichEdit）。
- OpenAI Chat Completions 协议与 SSE 流式解析器。
- Windows DPAPI 凭据加密与原子配置存储。
- 反向 OCR 等待适配器 (`IOcrAdapter`) 与命令行 `--snip`。
