# OCR AI Translator v0.9.1 Stable

正式稳定版，支持和验收平台为 **Windows 10 x64**。

## 下载与使用

- 下载 `OCR-AI-Translator-v0.9.1-Win10-x64.zip`，用同页 `OCR-AI-Translator-v0.9.1-Win10-x64-SHA256SUMS.txt` 校验后解压，运行 `Translator.exe`。
- EXE **未签名**；通常无需安装或管理员权限。Windows 可能提示未知发布者，请核对来源与 SHA256。
- 纯文本翻译不依赖 OCR。自行配置 HTTPS API 地址、模型 ID 和 API Key；云服务可能收费，密钥由当前用户 DPAPI 加密保存。
- 截图 OCR 可选依赖用户自行安装、授权的 **Snipaste 专业版桌面程序**，需在设置指定 EXE。本项目为独立工具，不捆绑 Snipaste。
- 默认关闭自动翻译、开启 OCR 候选确认。OCR 改变剪贴板；请核对候选后采用。Esc 取消本工具等待，不终止已有 Snipaste。

## 修复与兼容

- 未安装 Snipaste、路径为空或 EXE 缺失时，可以保存 Provider 并手动翻译文本。仅在启动 OCR 时校验 EXE，失败有明确提示，且不会发起翻译请求。
- 新配置和缺省字段采用谨慎默认值；已有显式设置保持不变。
- 补齐空路径／缺失 EXE 的保存、重启读取、OCR 失败无请求及独立文本翻译回归。
- GUI 自动化预热覆盖首次按钮各状态，保留原有严格的 GDI／USER／句柄稳态资源断言。
- 版本统一为 `0.9.1`，PE 移除预发布标志；打包要求最终提交 CI 通过，并核验托管 10 项全部 PASS。
- 沿用原配置目录、DPAPI、CLI、IPC 和 Profile，不迁移或重加密现有用户凭据。

## 验证与范围

本机 Windows 10 x64 Release 构建、完整 CTest、定向重复及最终提交的 Actions 结果见 [正式发布报告](https://github.com/rowanjove/OCR-AI-Translator/blob/main/docs/STABLE_RELEASE_REPORT_2026-10-15.md)。本机私有剪贴板若因 Win32 权限错误 5 跳过，按 SKIP 记录；正式打包要求最终 Hosted CI 对该项 PASS。

**开发者实机验收：真实 Snipaste OCR PASS、真实云 Provider PASS（据开发者确认）**。未提供实机原始审计日志；本轮未重复收费 API 请求，隔离 fixture 不冒充实机测试。

首次初始化曾观察到有限的 +1 GDI／句柄，具体对象来源未归因；50 次设置与 100 次渲染循环的严格稳态断言继续保留。没有证据将一次性变化认定为持续泄漏，也不宣称已归因系统缓存。

Windows 11、干净机、物理 IME、混合 DPI／多屏完整矩阵未验证，不属于本轮平台验收承诺。未提供代码签名、x86、ARM64 或其他操作系统版本。

ZIP 仅含生产程序、无密钥示例配置、使用说明、许可、依赖／构建信息和校验清单；不含 API Key、用户配置、DPAPI 文件、OCR 私人数据、测试 EXE 或内部日志。`BUILD_INFO.json` 记录源提交、版本、测试统计和 CI URL。
