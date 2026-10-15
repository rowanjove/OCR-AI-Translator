# OCR AI Translator v0.9.1 正式发布记录

日期：2026-10-15（Asia/Hong_Kong）。支持和验收平台：**Windows 10 x64 build 19045**。本报告先记录最终源码的本机验证；远端提交、CI 和附件校验凭证在公开发布后由仅文档提交补录。发布源码身份以 `v0.9.1^{commit}` 和包内 `BUILD_INFO.json` 为准。

## 版本与改动

- 目标 Tag：`v0.9.1`；目标公开页：[v0.9.1](https://github.com/rowanjove/OCR-AI-Translator/releases/tag/v0.9.1)。此前 preview Tag／Release 保留。
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
- GUI／进程／捕获／网络六项各三次的定向重复正在执行，发布前必须全部通过。原始日志、JUnit 和资源采样留在忽略的本地构建目录，CI 另存脱敏测试 artifact。
- 本机剪贴板隔离若因 Win32 错误 5 跳过，明确记 SKIP；不触碰用户剪贴板来凑 PASS。稳定版打包要求最终 Hosted CI 对其 PASS。
- **开发者实机验收 PASS（据开发者确认）：真实 Snipaste 专业版 OCR、真实云端 Provider。** 未提供原始实机审计日志；本轮沿用确认，不重复收费请求。自动化 fixture 与实机证据区分。

## 发布物与安全

- 目标 ZIP：`OCR-AI-Translator-v0.9.1-Win10-x64.zip`；外部校验文件：`OCR-AI-Translator-v0.9.1-Win10-x64-SHA256SUMS.txt`。
- 仅生产 EXE、无密钥示例、说明、许可、依赖／构建信息和校验；不带用户配置、API Key、DPAPI、私人 OCR、测试 EXE 或内部日志。程序 **未签名**。
- 公开候选源码审计 PASS：70 个文件，115 个公开历史 blob，0 项命中；提交前还核验精确 staged bytes。解压校验、远程下载回读和保护状态检查在完成后补录。

## 非阻断限制

- 初次设置／绘制初始化曾有有限 +1 GDI／句柄，具体对象来源未归因。按本轮发布规则记录并接受有界预热；持续增长仍由原有严格断言阻断。
- Windows 11、干净机、物理 IME、混合 DPI／多显示器完整矩阵未验证，不作为 Win10 x64 本轮发布承诺或阻断项。
- OCR 候选的剪贴板来源不能单靠序号变化认证；保留用户核对采用和安全暂停发送机制，推荐默认确认设置。
