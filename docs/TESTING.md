# Testing v0.9.1

支持和验收平台：Windows 10 x64 build 19045。最终构建、完整 CTest、重复测试、源提交和 Hosted CI 证据见 [正式发布报告](STABLE_RELEASE_REPORT_2026-10-15.md)。不使用旧提交成功状态代替最终提交检查。

## 自动化范围

全部 10 项 CTest 在 MSVC x64 Release 下运行：

| 测试 | 覆盖 |
| --- | --- |
| `ui_layout_geometry` | 缩放／布局组合的几何检查 |
| `probe_unit` | 命令解析与输入边界 |
| `native_clipboard_isolated` | 私有 window station 的剪贴板隔离 |
| `core_tests` | 配置原子保存／回滚、DPAPI、Profile、默认值、OCR 夹具、WinHTTP／SSE／超时 |
| `process_lifecycle` | CLI／IPC、退出、空 OCR 路径／缺失 EXE 保存和重载、无 OCR 的文本翻译 |
| `translator_gui_smoke` | 真实 HWND、设置保存／取消、候选确认／取消、主题与按钮绘制 |
| `capture_window_lifecycle` | 捕获窗口显隐、取消、会话与候选隔离 |
| `ui_stability_faults` | 100 次事件序列、100 次 OCR 会话、50 次设置、100 次渲染及严格资源断言 |
| `network_stability_faults` | 100 次 WinHTTP loopback 请求、取消／晚响应、SSE 分段及 reasoning 隔离 |
| `process_stability_faults` | IPC 压力、捕获与连接测试期间退出 |

CTest 向测试专用目标注入各自的 `SAT_TEST_MODE`／`SAT_CONFIG_DIR` 沙箱。测试使用虚构凭据、本地 HTTP/SSE 或 Snipaste 启动夹具，不调用收费云服务。生产程序不含这些测试入口或配置覆盖。

本机 `native_clipboard_isolated` 可能因 CreateWindowStation 权限错误 5 返回 77：记为 **SKIP**，不回退读写用户剪贴板。稳定版打包核验最终提交 Hosted CI 的 10 项全部 PASS，包括该隔离测试。

GUI 资源测试保持原有零增长断言和主题画刷释放检查。预热与稳态分别采样；有界首次初始化增量记录并接受，若测量循环持续增长则失败并阻断。未删测试、扩大容差或将 FAIL 改 SKIP。

## 重现

```powershell
./scripts/build.ps1 -BuildDirectory build/local
# 或在 VS 开发环境分别执行：
cmake -S . -B build/local -G "Visual Studio 17 2022" -A x64
cmake --build build/local --config Release --parallel 2
ctest --test-dir build/local -C Release --output-on-failure --output-junit "$PWD/build/local/ctest-release.xml"
ctest --test-dir build/local -C Release -R '^(process_lifecycle|translator_gui_smoke|ui_stability_faults)$' --repeat until-fail:3 --output-on-failure
```

构建使用 C++20、静态 CRT、`/W4 /WX`。构建日志、原始 JUnit、GUI 资源采样和包校验保存在忽略的本地构建目录；CI 上传本次测试证据，不上传用户数据和测试二进制。

## 实机验收与历史

- 真实 Snipaste 专业版 OCR、真实云 Provider：**开发者实机验收 PASS（据开发者确认）**，未提供原始审计日志。本轮沿用该确认，不重复收费调用。
- Windows 11、干净机、物理中文 IME、混合 DPI／多屏和无障碍完整矩阵未验证，不属于本轮 Win10 x64 发布承诺。
- EXE 未签名，如实披露。
- 历史 v0.9.0-preview 的本机 8 PASS / 1 FAIL / 1 SKIP 和早期 CI 失败保留在 [历史发布记录](GITHUB_RELEASE_READINESS.md)。v0.9.1-preview 基线 `73884295116a3ff27608d9ab72de17b1bbc32851` 的 [CI 38602597648](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38602597648) 为 10 PASS；仅作为基线证据。
- 上一轮新增回归和历史候选审查见 [候选快照](REVIEW_V0.9.1_2026-10-14.md)，其旧门槛结论已由本轮明确的 Win10 范围及开发者实机确认替代。
