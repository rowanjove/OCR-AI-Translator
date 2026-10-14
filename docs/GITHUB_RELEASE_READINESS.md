# GitHub 开源发布准备与验收报告

日期：2026-10-14（Asia/Hong_Kong）。候选版本：**v0.9.0-preview**。
名称保持 Snipaste AI Translator，产品定位为极轻量原生 Windows 翻译与文本处理工具，
支持可扩展 OCR 来源及自定义 AI API。Snipaste 是当前唯一实现的适配器，粘贴翻译独立可用。
本项目为独立工具；其他 OCR 来源属于计划。

## 源码公开授权更新（2026-10-14）

用户明确批准“先公开源码，暂不发布二进制”。目标仓库：
[Snipaste-AI-Translator](https://github.com/rowanjove/Snipaste-AI-Translator)，
GitHub CLI 已核实当前登录为 rowanjove。67 文件的暂存字节敏感审查重新 PASS。
本轮公开操作限于源码 main 分支，不创建 tag／GitHub Release，不上传候选 ZIP／EXE，
CI Artifact 仅包含测试证据。Apache-2.0、品牌及本地历史资料保持原有边界。
Pre-release／Stable 的资源门与实机验收 NO-GO 不变。

下文记录准备阶段的构建、测试、包哈希和当时未提交／无远程状态，属于历史审查快照。
源码推送及首个托管 CI 的实际结果将另行追加；不把授权或创建仓库当成 CI 成功证据。

## 1. 三类发布裁定

| 对象 | 结论 | 条件与证据 |
| --- | --- | --- |
| 公开源码 | **源码 GO，用户已批准仅公开源码** | 67 文件白名单及暂存字节审查通过，独立源码导出完整构建通过；Apache-2.0 与原创图标使用授权已由用户确认。没有创建远程或公开内容。历史代码每一行的独立来源证明仍有限。 |
| 公开 Pre-release 二进制 | **NO-GO** | GUI 资源零增长门 FAIL；远程 CI、MSVC 分发条件及对应实机验收未闭合。仅完成本地候选包，包含真实失败状态，允许继续诊断。 |
| Stable 正式发布 | **NO-GO** | 上述阻断以及真实 OCR／Provider、真人 IME、物理 DPI／多屏、Win11、干净机验收缺失。 |

完成发布准备不代表稳定性通过。当前候选仅供本地诊断与后续验收。

## 2. 本轮完成的工程整理

- 检查原始 2026-10-12 产品／技术规范、中文 README、CHANGELOG、最终稳定性审查，
  P0／P1／UI 阶段记录和实际 CMake、源码、测试、脚本。README.md 原不存在，现已补全。
  32 份原有文档的读取清单／哈希在本地 `out/github-readiness/review-inputs.json`。
  以本轮实际编译、JUnit 和代码为准，未沿用旧报告的阶段 PASS 代替验收。
- 原工程无 `.git`，初始化本地 `main`。没有旧历史可改写或扫描；未创建任何提交、标签或远程。
  67 个安全公开文件已暂存，供用户审阅后提交。未虚构 Git 身份或版权主体。
- 原始规范、历史审查／交接、私人证据、缓存、测试目录、用户配置和旧 release 均保留在本地。
  `.gitignore` 使用目录／文件白名单及敏感后缀防线；不是删除历史资料。
  本轮覆盖的 9 个原始源码／文档／配置文件已备份至 `out/github-readiness/baseline`。
- 完成双语 README、使用说明、贡献／安全说明、API／架构／UI／测试／资源合规文档。
  当前真实生产窗口的深浅主题截图已检查，无正文、Provider 字段或图像元数据。
- CMake 版本设为 0.9.0；生成头文件统一 0.9.0-preview。PE VERSIONINFO 含 0.9.0.0
  数值及 preview 字符串／预发布标志，帮助与关于页显示同版号与定位。
  未改动业务状态机、HTTP／OCR／密钥保存逻辑或测试断言，未新增业务功能／重命名模块。
- 新增 Windows x64 CI、暂存内容审查脚本和明确白名单便携打包脚本。

## 3. 公开仓库文件清单

完整公开清单见 [PUBLIC_FILES.txt](PUBLIC_FILES.txt)，**67 个文件**：

| 位置 | 数量／内容 |
| --- | --- |
| 根目录 | 12：CMake、双语 README、CHANGELOG、CONTRIBUTING、SECURITY、LICENSE、NOTICE、RELEASE_NOTES、config.example.json、Git 规则 |
| `src/` | 27：完整原生应用、OCR／网络／配置／解析／UI、探针、资源、版本模板和受测试宏保护的头文件 |
| `tests/` | 9：单元、配置、剪贴板、进程与压力夹具 |
| `scripts/` | 4：build、refresh-app-icons、audit-public、package-preview |
| `assets/app-icon/` | 4：已有 ICO、PNG、SVG 和生成脚本；参考原图／概念图保留本地 |
| `docs/` | 10：当前架构、API、UI、范围、测试、合规、准备报告、文件清单及 2 张截图 |
| `.github/workflows/` | 1：build.yml |

公开清单的实际数量与路径以 PUBLIC_FILES.txt 和最终 Git 暂存审查为准。
原始完整规范不直接公开其中的内部 Agent 流程，提炼为 PRODUCT_SPEC.md。
历史报告的私人路径和证据链接没有复制到公开文档；原件仍可本地复核。

## 4. 敏感信息审查

首轮全工作区扫描（新构建前）：**5,183 文件，509,873,548 bytes，24 ZIP，2,425 文本载荷**。
包括 docs/evidence、release、build、out 和历史源码／备份；ZIP 文本载荷解包检查，无不可读项。
未发现匹配规则的真实云端 API Key／Token／Authorization 明文。

发现并分类：164 个敏感文件名命中、96 个非空 DPAPI 引用命中、5,517 个本机私人路径命中。
这些是规则命中次数，不是唯一文件数；均留在本地并排除于公开清单。
11 个私钥头命中来自本机 TLS 夹具：8 个 build 临时 key.pem，以及动态生成私钥的脚本／历史副本。
核对脚本：每次创建短期回环自签证书并导出动态 RSA Key，没有静态真实私钥。
不将这些证书、Key、脚本和其凭据引用上传；未修改证书信任库或用户 Key。

- 公开工作树及准确 Git 暂存字节均扫描，**67 文件／0 finding，PASS**。
  最终暂存重扫结果保留在本地 `out/github-readiness/staged-scan-final.json`。
- 未有旧 Git 历史（unborn main），历史 blob 数 0；脚本支持未来全可达历史扫描。
- config.example.json 与真实 schema 相符，无 Key、空 credential_ref、通用 EXE 路径和
  `api.example.invalid` 虚构地址，关闭自动翻译并开启候选确认。隔离测试版离线读取：
  config_readable=true、configured=false、endpoint_valid=true、network_requests=0。
- 精确的虚构 Token、非空 DPAPI 引用、明文 Key、私人绝对路径拒绝用例均 PASS；
  只放行两个测试源文件中明确的 `Users/test/WindowsApps` 拒绝夹具，不放行真实用户名。
- 生产 EXE 的 ASCII／UTF-16 私人路径与 Mock／自测标记检查 PASS。
  包 ZIP 全部文本载荷再扫 PASS，明文或 DPAPI 密文都不进入包。
- 所有用户真实配置／凭据不复制到公开目录，不解密用于审查；3 次生产截图／解压烟测均
  验证配置原字节恢复与既有凭据密文不变。未发送真实收费 Provider 请求。

限制：这是规则扫描＋公开文本／图像人工复核，不是所有秘密的数学证明；大于 16 MiB 的文件和
二进制在首轮不作通用正文扫描，图像／非标准密钥／OS 内存可能不被规则发现。
公开源码二进制仅授权 ICO 和实际截图／图标 PNG，已视觉检查及去元数据核对。
DPAPI 密文不因加密而被视为可公开。后续新文件／新提交必须重审；禁止强制添加忽略目录。

## 5. 开源许可与来源

用户于本轮明确选择 **Apache-2.0**，并确认现有图标参考为本人原创、允许本项目公开使用。
已添加官方完整 LICENSE 与 NOTICE，保留原项目名、图标及所有权，没有指定新版权主体。
审查未发现 vendored 依赖、第三方版权／复制许可头；JSON／SSE／UI 代码为当前内置源码。
无旧 Git 历史，不能独立证明所有历史行的来源；不能宣称已通过全面法律认证。

原生系统 DLL／RichEdit／系统字体仅引用，不随包再分发；Snipaste、模型与工具链未打包。
Pillow 只属于可选开发脚本依赖，不是产品运行依赖。
静态 CRT 不意味着自动获得微软再分发权，公开二进制前应核对适用 VS2022 许可／REDIST 条件。
详见 [资源与第三方说明](THIRD_PARTY_NOTICES.md)，以及
[Microsoft 再分发说明](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)。

## 6. 实际构建与测试

环境：Windows 10 x64 build 19045；CMake 3.31.6、VS2022/MSVC 19.44.35226.0，
SDK 10.0.26100.0；C++20、Release x64、静态 `/MT`、`/W4 /WX`。

| 检查 | 结果 |
| --- | --- |
| 独立目录完整 Release 全目标构建 | PASS，非增量旧二进制 |
| 准确暂存树 git archive 导出后的独立 CMake／全目标构建 | PASS，不依赖忽略目录里的源码、测试或资源；代码与最终源码 EOL 规范化后同字节 |
| 全部注册 CTest | **8 PASS / 1 FAIL / 1 SKIP，96.09 秒，退出码 8** |
| capture_window_lifecycle／process_lifecycle／process_stability_faults | PASS，本机 HWND／假 Snipaste 启动器／本地请求；不是实际 OCR |
| ui_stability_faults | FAIL：GDI **56 → 57**，USER **64 → 64**，kernel handles **284 → 284**；主题自有画刷删除失败为 0；原零增长断言保留 |
| native_clipboard_isolated | SKIP：私有 Window Station Win32 5／返回 77；未回退访问用户剪贴板 |
| 其他 5 项逻辑／协议／GUI 夹具 | PASS；完整逐项列表见 TESTING.md |
| 深浅主题生产 HWND 截图／正常关闭 | PASS，无 API 请求，配置／凭据恢复；真人桌面体验 UNVERIFIED |
| PE 图标六档 16／24／32／48／64／256 与 ICO、16／32 Shell 像素 | PASS；Explorer／Alt+Tab 主观验收 UNVERIFIED |
| 包解压、CRC、逐文件哈希、PE x64、版本、测试入口排除、未签名状态 | PASS |
| 解压包实际生产启动／正常关闭及最终本项目进程残留 | PASS，最终 owned 残留 0；未结束任何用户 OCR 进程 |

CTest 显示的“90%”不能写成全部计划项通过：实际执行通过 8/9，全部计划通过 8/10。
未重复运行此前 ASan，历史 ASan／偶发夹具启动失败只是已有风险，不标成本轮验证通过。
最新正式稳定性审查原件 FINAL_STABILITY_AUDIT.md 在本地保留；本轮再现其 GDI 门失败。
没有为发布准备擅自扩大业务修复范围。细节见 [测试方法](TESTING.md)。

## 7. CI 状态

`.github/workflows/build.yml`：windows-2022、VS2022 x64、CMake Release，编译警告视为错误；
运行全部 CTest，显式保留返回码失败，单列 SKIP；不使用 continue-on-error、删除断言或隐藏错误。
源码／全可达历史审查、只读 contents 权限、不保留 checkout 凭据；官方 actions 使用已核对的
40 位 commit，checkout v4.3.1、upload-artifact v4.6.2。失败时仍上传测试证据 Artifact，不上传 EXE。无真实 Key、付费调用、Snipaste 授权、秘密配置或发布步骤。

YAML 解析、事件、Runner、命令目录、权限与 action pin 本地检查 PASS。
**首次 GitHub 托管 Runner 运行 UNVERIFIED**，当前没有远程仓库可运行。
本地失败资源门未因 CI 缺少远程证据而解除；Runner 的 GUI／剪贴板限制须单独报告，不能制造绿色。

## 8. 最终候选包

文件：`release/Translator-v0.9.0-preview-Win10-x64.zip`；**446460 bytes**。
ZIP SHA-256：`3C52F1B0F63D712710CE6321355EAF51D8DE3C573A59DBD5BBCFB203E1AD42FB`。
生产 EXE：**1,156,608 bytes**；SHA-256：`637BF1008FB2CC0DBD41385E1699D28BFBFC732EF662030382AC55C2A6EAB412`。
版本字符串 0.9.0-preview，数值 0.9.0.0，PE machine 0x8664；Authenticode **NotSigned**。

11 个明确有效载荷：Translator.exe、USER_GUIDE.zh-CN.md、RELEASE_NOTES.md、LICENSE、NOTICE、
config.example.json、THIRD_PARTY_NOTICES.md、PREVIEW.txt、BUILD_INFO.json、DEPENDENCIES.txt、SHA256SUMS.txt。
中文说明采用英文文件名，避免 ASCII 校验文件破坏中文文件名；最初打包尝试已保留于本地 out，
重新打包并核对 UTF-8 说明内容、ASCII 路径、ZIP CRC 与全部 SHA-256。
BUILD_INFO 包含工具链版本、源码文件哈希、JUnit 哈希、真实 8/1/1 统计和 NO-GO 状态。
包内校验清单校验其他载荷；ZIP 自身校验在包外 release/SHA256SUMS.txt，避免循环哈希。
准备报告在源码中，未复制到 ZIP 内，因而后续写入 ZIP hash 不产生自引用或包内过期报告。

实际静态导入：USER32、GDI32、bcrypt、SHELL32、ADVAPI32、dwmapi、CRYPT32、WINHTTP、COMCTL32、
ole32、IMM32、COMDLG32、d2d1、KERNEL32；动态从 System32 加载 Msftedit.dll。
没有 Qt／Electron／WebView2／模型／Python 产品运行时／非系统 DLL。
本轮按明确允许失败的本地诊断候选参数生成，默认打包脚本会拒绝失败套件。
该允许不使测试或公开发布变成 GO。

## 9. 证据保留与复现

公开文档不分发私人原始日志；本地以下证据完整保留（路径为工程相对位置）：

| 本地证据 | SHA-256 |
| --- | --- |
| `out/github-readiness/configure.log` | `F8935C61337C250FC36392D52961F1A16A338695D3FF8579DB57C26831B35FF7` |
| `out/github-readiness/build.log` | `9152F9054407C1C80AD596B970E58534E16E9B2754C2B24D8F03DCFE963744EE` |
| `out/github-readiness/ctest.log` | `8EE69195032D8F81C8E7AAA31D15003E1238BA473D8ACED31C186533589BBCBB` |
| `out/github-readiness/ctest.xml` | `D2FEAB23F2ADA8C980E1A06247ADA001B3B576C7D508447712264B1BC76837E3` |
| `out/github-readiness/clean-configure.log` | `15E3F88B7D88DB38CCF9D4E51BCDE615BE6A5CEB3C12FF636926780124E1FD0A` |
| `out/github-readiness/clean-build.log` | `6764349E03F482DD7FD44BEB8DD075E4B24D95BD3F4E0BEFADBAE50E7B44C8F4` |
| `build/github-preview/stability-ui-config/gui-stability.json` | `3CBE609C24EF85EE15D37AC35689438589CFB4096E037E658DEFF1C84E7552CF` |
| `out/github-readiness/package-final-checks.json` | `236ADB6C77D4E63A409B286F882A1E100CB66B86D16481C2723CACB2887A30D5` |
| `out/github-readiness/staged-scan-initial.json` | `A8C41AC27C5DC96E8C79854B38501D06364AA5234E142B881496C03CB40CD3C5` |

补充：初次全工作区扫描 initial-sensitive-scan.json、最终暂存扫描 staged-scan-final.json、
源码基线、配置样例离线结果、截图／图标／包烟测、独立导出／构建日志均在 ignored out/build 中。
扫描输出只记录规则／位置／摘要，不记录 Key 或正文。
复现命令见 README 与 TESTING；`python scripts/audit-public.py --staged` 审查准确准备提交的字节。
本地 Git 处于 main 未提交状态，67 个公开文件暂存；无 remote、无 push、无 Release 或外部账号操作。
不要把整个工作区直接上传或强制添加 ignored 目录。

## 10. 未决风险与用户确认事项

1. GDI 56 → 57 的具体对象／创建释放堆栈未定位；失败资源门为 Pre-release／Stable 阻断。
   不把非线性增长或其他计数不增长解释为无泄漏，也不加容差／固定 sleep 放行。
2. 真实 Snipaste 专业版 OCR、来源关联、竞争复制、同文／Esc／连续截图；最终 EXE 的真实
   HTTPS Provider／账单／代理／思考模式；真人 IME、无障碍、物理混合 DPI／多屏、Win11、干净机
   都仍 UNVERIFIED，需要对应环境证据。历史单请求成功不代替本候选验收。
3. CI 首次远程运行、MSVC 适用再分发条件仍待核验；二进制未签名已明确披露。
4. Apache-2.0 及图标原创授权已经确认，无需重复选择。源码历史完整来源证明有限，所有者应在
   公开前审阅 67 文件及 NOTICE。现有品牌已经保留，不提出或执行额外改名。
5. **最终需要用户明确批准的外部操作**：创建／公开远程仓库、推送源码或发布 GitHub Release。
   它们本轮均未执行；源码批准与二进制 Pre-release／Stable 放行是独立决定。

本轮实际交付为本地可审阅的开源源码候选＋诚实标记失败的便携诊断候选。
