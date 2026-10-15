# OCR AI Translator v0.9.1 稳定性修复审查

> 历史候选审查快照：保留原始测试结果与当时结论。2026-10-15 开发者确认真实 OCR／Provider 通过，并限定 Win10 x64；当前发布结论见 [正式发布报告](STABLE_RELEASE_REPORT_2026-10-15.md)。以下 NO-GO 属于此前门槛，不代表当前状态。

> 对照任务书：`OCR_AI_Translator_v0.9.1_Stabilization_Plan_2026-10-14.md`
> 审查日期：2026-10-15（任务书生成日期：2026-10-14）
> 判定范围：v0.9.1 稳定性修复预览候选；不代表 Stable 发布验收。

## 结论

- **GO：可作为 v0.9.1 稳定性修复预览候选供审查。** Snipaste 可选性、默认值兼容和设置预热回归均有代码及本机自动化证据；Windows x64 Release 全量 CTest 为 9 PASS / 0 FAIL / 1 SKIP，GUI 稳定性测试重复通过，资源断言未放宽。
- **NO-GO：不可据此标记 Stable 或宣称完整实机验收。** 设置首次预热后有一个来源未识别的净 GDI 对象；Windows 11、干净机、真实 Snipaste OCR、真实云 Provider、物理 IME、混合 DPI／多屏仍未验证。剪贴板隔离测试在本机因 Win32 错误 5 跳过。

## 基线与工作区

- 仓库：`仓库根目录`；分支 `main`；审查基线 HEAD：`73884295116a3ff27608d9ab72de17b1bbc32851`，与 `origin/main` 相同。
- Windows 10 x64 build 19045；Visual Studio 2022 / MSVC 17.14.40；Windows SDK 10.0.26100.0；CMake 3.31.6；PowerShell 7.6.5。
- 开始时工作区已有未提交内容：`CHANGELOG.md`、`CMakeLists.txt`、`README.en.md`、`README.md`、`RELEASE_NOTES.md`、`docs/API_COMPATIBILITY.md`、`docs/PRODUCT_SPEC.md`、`docs/TESTING.md`、`scripts/package-preview.ps1`、`src/ui_visual_tests.hpp`。保留了与本任务无冲突的现有编辑；将版本／产品状态中与“候选、非 Stable”目标冲突的元数据恢复为 preview。没有触碰用户配置、DPAPI 文件、私有 OCR 内容或 API 密钥。
- 本轮新增 `tests/process_tests.cpp` 的空路径与缺失路径回归覆盖，更新候选验证文档并生成本报告；在 `.gitignore` 的文档白名单中单独允许此审查报告。当前工作区仍有未提交改动；GitHub Actions 对应的是基线提交，不包含本地新增用例及未提交文档。

## 问题复核与根因

| 线索 | 状态与证据 | 最小修复／现状 |
| --- | --- | --- |
| A. 保存 Provider 错误依赖 Snipaste | **CONFIRMED，已修复。** 旧 `settingSave` 分支对 `candidate.snipasteExePath` 调用 `SnipasteReverseAdapter::validateExecutable()`；该检查要求目标文件可用，所以没有安装 Snipaste 时，保存无关的 Provider 设置也会失败。提交 `7d32176` 删除了保存阶段的可执行文件校验。当前 `settingsProc` 保存配置时只保留设置字段校验并调用 `saveConfig`（`src/translator.cpp`）；路径仅作长度、引号及换行等基本检查（`src/config_store.cpp`）。 | OCR 启动时仍在 `SnipasteReverseAdapter::startCapture()` 调用 `validateExecutable()`（`src/ocr_adapter.cpp`），缺失或非法 EXE 会给出错误，不会回退到 PATH／Store alias。空路径和不存在路径可保存；本地 fixture 验证 Provider 保存、重启读取、OCR 清晰失败且没有 Provider 请求、手动文本翻译成功。 |
| B. CI 设置预热报 `settings warmup did not close` | **CONFIRMED，根因与 A 一致。** `src/stability_ui_tests.hpp` 的预热循环交替保存／取消设置。CI 没有 Snipaste 时，保存轮次因旧校验失败而让设置窗口保持打开，随后关闭断言失败；这与资源泄漏不是一回事。当前测试显式使用确定不存在的 OCR 路径，恰好覆盖“无 Snipaste 仍可保存”情形；保存校验移除后，窗口关闭和重载断言通过。 | 旧 Actions run `38578032811` 出现该失败；后续 run `38602597648` 对提交 `7388429…` 的审计、Release 构建和 10 个 CTest 均成功。当前本机全量套件和 UI 稳定性重复也通过。无法据一次 CI 成功断言所有 Runner 调度抖动均已排除。 |
| C. GUI GDI／USER／进程句柄增长 | **历史失败已复核；当前严格循环未复现，仍有未归因项。** 旧本机／CI 运行曾触发资源增长断言。当前 `ui_stability_faults` 记录首次设置预热、quiescence、循环轨迹；50 次设置和 100 次主题／缩放／显隐循环继续使用零增长断言，未删断言、未加容差、未将 FAIL 改为 SKIP。 | 最新全量运行中，设置预热 GDI 为 `56→64→65→57→57`，USER 为 `65→190→190→65→65`，句柄 `283` 全程不变；预热后比预热前净多一个 GDI，来源未知，不推定为系统缓存。严格 50 次设置：GDI `57→55`、USER `65→63`、句柄 `283→283`。严格 100 次渲染循环：GDI `57→57`、USER `64→63`、句柄 `283→283`；主题画刷删除失败数为 0。该净 GDI 来源仍须追踪。 |
| D. 首次安装默认值不一致 | **CONFIRMED，已修复并有回归测试。** 默认值现为 `autoStart=false`、`confirmOcrCandidates=true`（`src/config_store.hpp`）；旧配置缺字段时相同地采用谨慎默认值（`src/config_store.cpp`）。 | `tests/core_tests.cpp` 覆盖全新配置、缺字段写回、显式 `true/false` 原值往返及其他配置字段保留；`tests/process_tests.cpp` 检查首次设置 UI 默认状态。显式保存的既有用户选择不会被新默认覆盖。 |

### A 的定向回归路径

`process_lifecycle` 使用隔离配置目录和本机 HTTP/SSE fixture，不用真实账号或收费 Provider：

1. 启动默认配置，确认自动翻译关闭、OCR 候选确认开启。
2. 将 Snipaste 路径置空，保存 Provider；检查 JSON 保留空路径和 Provider 配置且不写入明文 API key。
3. 重启确认空路径及 Provider 重载；尝试 OCR，得到明确 EXE 错误且无网络请求。
4. 使用本地 HTTP fixture 做手动文本翻译，证明普通文本流程不需要 OCR 安装。
5. 再将路径设为不存在的 EXE，保存并重启；Provider 保留，OCR 再次明确失败且无新 Provider 请求。

覆盖边界：此用例验证的是隔离 GUI 文本输入／手动翻译路径；当前主机的私有剪贴板隔离失败，因此不能声称真实系统剪贴板粘贴路径已在此轮通过。

## 修改文件与范围

- `tests/process_tests.cpp`：增加空路径与缺失路径保存、重启、OCR 错误／无请求及本地 fixture 手动翻译回归。没有修改产品运行时代码；A、D 的运行时代码修复已存在于基线提交历史中，本轮通过源代码、历史差异及测试独立复核。
- `.gitignore`：将本次要求交付的审查报告加入 `docs/` 白名单。
- `docs/TESTING.md`、`CHANGELOG.md`、`RELEASE_NOTES.md`：更新当前全量测试时间、最新资源采样、Actions 结果与未验证矩阵。
- `docs/REVIEW_V0.9.1_2026-10-14.md`：本审查报告。
- 起始工作区已有的 `README.md`、`README.en.md`、`scripts/package-preview.ps1`、`src/ui_visual_tests.hpp` 改动予以保留。未运行打包脚本；未生成发布包、未推送、未创建 PR、未打 tag、未更改 GitHub Release。
- 版本仍为 `0.9.1-preview`。没有新增功能、协议、全局快捷键或 OCR 后端；没有大规模拆分 `translator.cpp`；配置目录、DPAPI entropy、window class、mutex、IPC 及公开 CLI 入口未改。

## 构建与测试证据

实际工具路径：

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
```

执行记录：

| 命令／检查 | 结果 |
| --- | --- |
| `cmake -S . -B build/v091 -G "Visual Studio 17 2022" -A x64` | **PASS**；Windows 10.0.19045 目标，SDK 10.0.26100.0。 |
| `cmake --build build/v091 --config Release --parallel 2` | **PASS**；Release 全目标构建完成，包括 `Translator.exe`、`ProcessTests.exe`、`TranslatorTest.exe` 与 UI／网络／核心测试目标。 |
| `ctest --test-dir build/v091 -C Release -R '^process_lifecycle$' --output-on-failure` | **PASS**，1/1，16.25 秒；包含本轮新增的空／缺失路径断言。 |
| `ctest --test-dir build/v091 -C Release -R '^ui_stability_faults$' --repeat until-fail:3 --output-on-failure` | **PASS**，3/3 连续运行，合计 85.25 秒。 |
| `ctest --test-dir build/v091 -C Release -R '^translator_gui_smoke$' --repeat until-fail:3 --output-on-failure` | **PASS**，3/3，合计 29.19 秒。 |
| `ctest --test-dir build/v091 -C Release --output-on-failure --output-junit build/v091/ctest.xml` | **PASS**，全量 10 项：9 PASS / 0 FAIL / 1 SKIP，77.67 秒，退出码 0。最新 JUnit：`build/v091/build/v091/ctest.xml`。 |
| `git diff --check` | **PASS**。Git 对既有 CRLF/LF 文件给出换行规范提示，没有空白错误。 |

全量测试中的唯一 **SKIP** 是 `native_clipboard_isolated`：Windows 本机不能创建私有 window station，Win32 error 5；测试没有读取或写入用户剪贴板。该项在下述 Hosted Actions run 中通过。`ui_layout_geometry` 的 35,520 个缩放／布局组合是模拟检查，不等价于实体 DPI／多屏验收。网络和 Provider 测试使用真实 WinHTTP 到本机 fixture；没有真实收费 API 调用。

## GitHub Actions

- **PASS：** [run 38602597648](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38602597648)，提交 `73884295116a3ff27608d9ab72de17b1bbc32851`。审计、Windows x64 Release 构建、10 个 CTest 全部通过；Hosted Runner 上 `native_clipboard_isolated` 也通过。
- **历史失败：** run `38578032811` 的设置预热关闭断言失败；run `38598323681` 的 GUI 资源断言失败。两种失败分别记录，不能把资源失败归因给设置关闭错误。
- `38602597648` 对应提交版本，不包含本工作区新增的空路径／缺失路径扩展及本报告。当前确切本地工作树由上表全量 CTest 验证。没有为当前未提交工作树触发新的 CI。

## 验收矩阵

| 项目 | 状态 | 证据／限制 |
| --- | --- | --- |
| Windows 10 x64 构建与原生 GUI 自动化 | **PASS** | 本机 Win10 build 19045；Release 构建及完整 CTest。测试使用真实 HWND 和本地 fixture。 |
| Windows 11 | **UNVERIFIED** | 无 Win11 设备／虚拟机运行证据。 |
| 新配置谨慎默认与旧字段兼容 | **PASS** | core/process 回归覆盖默认值、显式反向值和缺字段写回。 |
| 无 Snipaste 时保存 Provider 与手动文本翻译 | **PASS（fixture）** | 空路径与缺失 EXE 的隔离 GUI/local HTTP 测试；不是干净机安装验收，也不是物理剪贴板验收。 |
| 真实 Snipaste 专业版 OCR／剪贴板竞争 | **UNVERIFIED** | 未启动真实 Snipaste；OCR adapter 仅用 fixture 验证启动约束。 |
| 真实云 Provider、HTTPS／代理／证书／流式收费链路 | **UNVERIFIED** | 测试用本机 HTTP/SSE fixture，无真实账号、付费请求。 |
| 私有剪贴板隔离 | **SKIP（本机）** | 创建 private window station 返回 Win32 5；没有退化到访问用户剪贴板。Hosted CI 为 PASS。 |
| 实体中文 IME | **UNVERIFIED** | 自动化不替代人工输入法验收。 |
| 实体混合 DPI／多显示器及可访问性 | **UNVERIFIED** | 有模拟布局组合，不代表物理设备、焦点、屏幕阅读器验收。 |
| 干净机安装、长时间运行与真实用户配置迁移 | **UNVERIFIED** | 本轮只用隔离配置与虚构密钥；没有接触真实配置或密钥。 |

## 遗留风险与后续动作

1. **P1：预热后一个净 GDI 对象来源未明。** 当前严格测量的 50 次设置和 100 次渲染循环均无增长，重复测试可通过；但 `56→…→57` 的一次净增长还不能归因为 Windows 合法缓存。后续应在相同 Win10 版本上追踪拥有者／创建与销毁点，并在独立 Windows 环境复测；若严格循环出现正增长，必须保持测试失败并修复来源。
2. **P1：设备与真实 Provider 矩阵未完成。** 在 Win11、干净机、真实 Snipaste、真实 Provider、IME、混合 DPI／多屏验收完成前，不将候选升级为 Stable，也不宣称正式稳定版验收。
3. **P2：本机剪贴板 SKIP。** 在可创建隔离 window station 的 Windows 环境重跑 `native_clipboard_isolated`；禁止通过接触用户剪贴板来换取 PASS。

## 最终判定

**GO：v0.9.1 稳定性修复预览候选，可供代码与候选材料审查。** 已有足够本机证据证明目标修复路径、默认值兼容和当前 GUI 稳态循环；完整 Release CTest 无 FAIL。此 GO 不覆盖未提交工作树的 Hosted CI，也不覆盖真实 Snipaste／云 Provider／Win11 等验收。

**NO-GO：Stable 发布／正式实机验收。** 一个 GDI 预热增量仍未归因，实机矩阵尚未完成。本轮没有推送、发布或改动远端 Release。建议继续完成 v0.9.1 候选验收闭环，再讨论 Stable；不将当前证据视作 v1.0 的发布门槛通过。
