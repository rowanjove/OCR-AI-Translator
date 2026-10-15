# OCR AI Translator 发布记录

当前版本：**v0.9.1 Stable（Windows 10 x64）**。本轮提交、CI、资产校验和验收范围见 [正式发布报告](STABLE_RELEASE_REPORT_2026-10-15.md)。

以下为 v0.9.0-preview 历史记录，保留当时失败与限制，不作为当前版本结果。

日期：2026-10-14（Asia/Hong_Kong）。版本：**v0.9.0-preview**。
用户明确要求使用通用产品名称、中文默认 README／英文独立页，并批准打包公开此预发布版。
授权适用于本次公开源码和便携 ZIP；测试失败和未完成验收仍按实际状态记录，不宣称 Stable。

## 产品与仓库

- 产品／窗口／关于页／EXE 属性：**OCR AI Translator**。
- 仓库：[rowanjove/OCR-AI-Translator](https://github.com/rowanjove/OCR-AI-Translator)。
- 中文默认主页：[README.md](../README.md)，英文独立页：[README.en.md](../README.en.md)。
- Snipaste 仅为目前实现的外部 OCR 适配器；调用方法在 README 的专门章节。
- 更多 OCR 工具及内置 OCR 属于未来计划，不冒充当前支持。
- 为兼容既有设置，保留旧版配置目录、DPAPI entropy、window class／mutex／IPC 标识。
  这不影响公开品牌；没有迁移、删除或重加密用户真实 API Key。
- 保留 Apache-2.0、原创图标及项目所有权，未重新生成品牌图标。

## 实际构建与测试

Windows 10 x64 build 19045、VS2022/MSVC 19.44、SDK 10.0.26100.0、CMake 3.31.6。
Release 全目标构建 PASS，静态 CRT、C++20、`/W4 /WX`。
最终完整 CTest：**8 PASS / 1 FAIL / 1 SKIP，98.87 秒，退出码 8**。

| 测试 | 最终结果 |
| --- | --- |
| `ui_layout_geometry` | PASS |
| `probe_unit` | PASS |
| `native_clipboard_isolated` | SKIP |
| `core_tests` | PASS |
| `process_lifecycle` | PASS |
| `translator_gui_smoke` | PASS |
| `capture_window_lifecycle` | PASS |
| `ui_stability_faults` | FAIL |
| `network_stability_faults` | PASS |
| `process_stability_faults` | PASS |

`ui_stability_faults` 严格 GDI／USER／kernel 零增长断言仍 FAIL；未删除断言或扩大容差。
`native_clipboard_isolated` 因本机私有 Window Station 权限不足 SKIP，未回退访问用户剪贴板。
此前 [托管 CI](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38578032811)
为 9 PASS / 1 FAIL，压力测试在设置预热断言提前失败，根因尚未定位。
本次源码推送会再次触发正常 CI，当前 Release 不把先前证据冒充本版托管 CI 成功。

用户反馈本地使用正常，并在知悉已有自动化结果后明确要求公开预览包。
这项发布决定不把 FAIL／SKIP 转为 PASS；Stable 完整验收仍未完成。
真实 OCR／Provider、真人 IME、物理混合 DPI／多屏、Win11 和干净机完整矩阵仍未验证。

## 隐私与包检查

- 本轮公开文件清单为 **68 个文件**，见 [PUBLIC_FILES.txt](PUBLIC_FILES.txt)，新增英文分页。
- 工作树及提交前敏感扫描通过，扫描包含可达 Git 历史；原始记录留在本地 out。
- 当前生产 EXE 深浅主题真实 HWND 截图已检查，新标题完整显示；配置原字节恢复、既有凭据密文不变。
- 包只含生产 EXE、无密钥示例配置、用户说明、Release Notes、许可、依赖／构建信息和校验清单。
  不含真实 API Key、用户 config.json、DPAPI 密文、OCR／翻译正文、截图私图、内部日志、测试 EXE／PDB／缓存。
- 解压逐文件哈希、ZIP CRC、校验清单、生产测试入口排除、PE x64／品牌／版本检查后发布。
- EXE **未签名**，不宣称 Windows 代码签名认证。运行依赖为 Windows 系统 DLL 和动态 RichEdit，
  静态 CRT；不含 Snipaste、模型、字体或第三方服务程序。
- 资源授权和工具链条件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
  用户已选择 Apache-2.0 并确认图标原创使用权；扫描与依赖清单不构成全面法律认证。

## 下载

[GitHub v0.9.0-preview](https://github.com/rowanjove/OCR-AI-Translator/releases/tag/v0.9.0-preview)。
发布为 **Pre-release**，不标记稳定正式版。便携资产：
`OCR-AI-Translator-v0.9.0-preview-Win10-x64.zip`；包外校验资产：
`OCR-AI-Translator-v0.9.0-preview-SHA256SUMS.txt`。

包大小：447257 bytes；SHA-256：`87F102711F7775E32E5763D392279F3D9AF14E28D8D35E1126A7847A79644834`。
EXE SHA-256：`6E154611F949D2865B6892342E43BC9011707F2BA19687C5971082FAACA98A86`。
本地 ZIP CRC、包内／包外 SHA-256、解压逐文件比对、源码／JUnit hash 和敏感模式扫描均 PASS。
解压后的生产程序真实启动／关闭 PASS，配置／凭据未改变。
远程 Release 已公开（isDraft=false／isPrerelease=true），2 个资产均完成下载回读；
GitHub 资产 digest、下载文件与本地文件的 SHA-256／大小逐项一致，PASS。
Release tag 指向 `518c5eb30483869b698b1b3ee5f0094d654acb67`，对应本次构建源码。
报告不复制到 ZIP 内，避免循环哈希。
详细步骤和实际测试状态见 [RELEASE_NOTES.md](../RELEASE_NOTES.md) 与 [TESTING.md](TESTING.md)。

原始准备阶段、首次源码发布及旧包的记录由 Git 历史和本地备份保留，没有删除旧产物或改写历史。
