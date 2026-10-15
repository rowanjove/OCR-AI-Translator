# OCR AI Translator v0.9.1 正式发布记录

日期：2026-10-15（Asia/Hong_Kong）。**v0.9.1 Stable 已公开发布**，`draft=false`、`prerelease=false`，页面与两个附件均可匿名访问。支持和验收平台：**Windows 10 x64 build 19045**。此为发布后仅文档证据补录，不改变已通过 CI 的发布源码、Tag 或二进制。

## 交付身份

- 最终发布源码提交：[`190728f376963e80c9bd88e97f092243796819e1`](https://github.com/rowanjove/OCR-AI-Translator/commit/190728f376963e80c9bd88e97f092243796819e1)。
- 正式 Tag：`v0.9.1`，annotated tag 对象 `60f4c986755e36faf6623f1151437f82c8a65b83`；远端解引用到上述源码提交。已有 `v0.9.1-preview` 仍指向 `73884295116a3ff27608d9ab72de17b1bbc32851`，未覆盖或删除。
- [正式 Release](https://github.com/rowanjove/OCR-AI-Translator/releases/tag/v0.9.1) · [Win10 x64 ZIP](https://github.com/rowanjove/OCR-AI-Translator/releases/download/v0.9.1/OCR-AI-Translator-v0.9.1-Win10-x64.zip) · [SHA256SUMS](https://github.com/rowanjove/OCR-AI-Translator/releases/download/v0.9.1/OCR-AI-Translator-v0.9.1-Win10-x64-SHA256SUMS.txt)。
- ZIP：`OCR-AI-Translator-v0.9.1-Win10-x64.zip`，**446255 bytes**，SHA256：`DD334D3CFBF687AF33543AE761C296B3C56A42CA51A9EBBAAC0EE9CA2624D23C`。
- EXE：`Translator.exe`，1156608 bytes，SHA256：`0A52F5CC03AE03F7F7E319116C130D86F212C42C81D1D3AC563EC9D2261AD78F`；Product/FileVersion `0.9.1`、固定数值版本 `0.9.1.0`、x64 PE、`IsPreRelease=false`、`NotSigned`。

## 版本与改动

- 此前 preview Tag／Release 保留，正式版使用独立新 Tag 与 Release。
- `CMakeLists.txt`、`src/app_resources.rc`：版本统一为 `0.9.1`，PE 移除预发布标志。`src/translator.cpp` 仅更新 About 的 Win10 支持与可选 OCR 说明，不改 OCR／网络处理。
- `tests/process_tests.cpp`：保留上一轮空路径／缺失 EXE 的配置保存、重启、OCR 明确失败无请求和独立文本翻译回归。
- `src/ui_visual_tests.hpp`：保留首次按钮状态预热和资源失败计数诊断；严格资源断言不变。
- `scripts/package-preview.ps1`：保留脚本入口，支持正式语义版本；强制核验同一最终提交的 push CI 及 Hosted 10 项全部 PASS，检查生产标记、PE、依赖、签名、ZIP 逐文件 SHA256。
- `scripts/audit-public.py`：历史审计覆盖 HEAD、全部分支、Tag 和远端引用；不将 Codex 私有工作区快照引用视作公开历史。原扫描发现的旧私有路径只存在本机快照，未在可发布历史中出现；保留快照，不删除历史或绕过真实公开内容检查。凭据／私有路径负例与无密钥示例检查 PASS。
- README 中英文分页、CHANGELOG、RELEASE_NOTES、API／产品／测试文档更新发布范围；历史候选报告原测试记录保留并标明已被本轮范围替代。公开清单加入两份报告。
- 生产业务修复原已包含在基线 `73884295116a3ff27608d9ab72de17b1bbc32851`：Provider 保存不依赖 Snipaste，OCR 启动仍校验 EXE，默认自动翻译关闭、候选确认开启。无额外业务重构、配置迁移或凭据重加密。

## 验证

- 在新建的 `build/stable-v091` 使用 VS2022/MSVC 19.44.35226、CMake 3.31.6、SDK 10.0.26100.0，C++20、Release x64、静态 CRT、`/W4 /WX`。
- 独立新目录首次完整构建／CTest：**9 PASS / 0 FAIL / 1 SKIP，86.40 秒**；修正 About 过时文字后，最终全目标 Release 构建及完整 CTest：**9 PASS / 0 FAIL / 1 SKIP，86.15 秒**，退出码 0。编译无警告或错误。
- 最终完整套件：50 次设置 GDI `57→55`、USER `67→64`、句柄 `308→308`；100 次主题／缩放／显隐 GDI `57→57`、USER `65→65`、句柄 `308→308`；主题画刷释放失败 0。未修改稳态断言。
- `process_lifecycle`、`translator_gui_smoke`、`capture_window_lifecycle`、`ui_stability_faults`、`network_stability_faults`、`process_stability_faults` 各连续三次：**18 PASS / 0 FAIL / 0 SKIP，206.36 秒**。最后一次资源采样与完整套件上述数据相同。原始日志、JUnit 和采样留在 `out/stable-v091`、`build/stable-v091`，均被 Git 忽略。
- 最终发布源码的 [push CI 38662439430](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38662439430)：**PASS**。源码审计、MSVC x64 Release `/W4 /WX` 构建、全部 **10 PASS / 0 FAIL / 0 SKIP**；下载 CI artifact 的原始 JUnit 独立核验，包括 `native_clipboard_isolated` PASS。判定只采用上述最终提交，未用基线绿灯代替。
- 同一源码提交的 [正式 Tag CI 38662756329](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38662756329) 也已 **PASS**，未覆盖 Tag 来规避任何检查。
- 打包脚本使用旧提交成功 CI 的负例测试：正确拒绝，不生成发布包；实际打包再次在线核验最终提交 SHA、push 事件与完整托管测试证据。
- 本机剪贴板隔离若因 Win32 错误 5 跳过，明确记 SKIP；不触碰用户剪贴板来凑 PASS。稳定版打包要求最终 Hosted CI 对其 PASS。
- **开发者实机验收 PASS（据开发者确认）：真实 Snipaste 专业版 OCR、真实云端 Provider。** 未提供原始实机审计日志；本轮沿用确认，不重复收费请求。自动化 fixture 与实机证据区分。

## 发布物与安全

- ZIP 与外部校验文件已上传，两个资产状态均为 `uploaded`。Release 页面匿名 HTTP 200；两个附件分别匿名下载，大小、本地 SHA256、GitHub `digest` 均逐项一致。外部校验文件 106 bytes，SHA256：`3BE29995DDBEE81FEB4366ED8D62A03569ABE3B4D577BB2F9030A918D5D19E16`。
- 仅生产 EXE、无密钥示例、说明、许可、依赖／构建信息和校验；不带用户配置、API Key、DPAPI、私人 OCR、测试 EXE 或内部日志。程序 **未签名**。
- 公开候选及精确 staged bytes 审计 PASS：70 个文件、115 个公开历史 blob，0 项命中；最终提交 Hosted 审计为 70 个文件、134 个历史 blob、0 项命中。
- 本机独立包检查 **PASS**：严格 11 文件白名单、ZIP CRC、解压逐文件 SHA256、包内清单、包外清单、敏感文本扫描、生产测试入口排除、版本／源提交／编译源码哈希一致性。只依赖 Windows 系统 DLL，动态 RichEdit 来自 System32；无第三方 EXE／模型／字体。
- 解压后的生产 EXE 执行离线 `--provider-check translation` 启动／退出 **PASS**，退出码 0，`network_requests=0`；此项不是云 Provider 在线验收，也不冒充人工 GUI 操作。
- 真实用户配置、备份及 DPAPI 文件 SHA256 与任务开始基线一致；既有 Snipaste 进程保留。未重置或覆盖用户工作区增量，未读取私人 OCR 正文或调用真实付费 API。

## 非阻断限制

- 初次设置／绘制初始化曾有有限 +1 GDI／句柄，具体对象来源未归因。按本轮发布规则记录并接受有界预热；持续增长仍由原有严格断言阻断。
- Windows 11、干净机、物理 IME、混合 DPI／多显示器完整矩阵未验证，不作为 Win10 x64 本轮发布承诺或阻断项。
- OCR 候选的剪贴板来源不能单靠序号变化认证；保留用户核对采用和安全暂停发送机制，推荐默认确认设置。
