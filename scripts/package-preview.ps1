param(
    [string]$BuildDirectory = 'build/github-preview',
    [string]$TestReport = 'out/github-readiness/ctest.xml',
    [switch]$AllowFailingPreview
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "$BuildDirectory/Release/Translator.exe"
$testPath = Join-Path $root $TestReport
[xml]$xml = Get-Content -LiteralPath $testPath -Raw
$cases = @($xml.testsuite.testcase)
$failed = @($cases | Where-Object { $null -ne $_.failure }).Count
$skipped = @($cases | Where-Object { $null -ne $_.skipped }).Count
$passed = $cases.Count - $failed - $skipped
if ($cases.Count -ne 10) { throw 'Expected all 10 registered CTest cases.' }
if ($failed -gt 0 -and -not $AllowFailingPreview) { throw 'CTest failed. Only an explicit local diagnostic preview may be packaged with -AllowFailingPreview.' }
$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($version -ne '0.9.0-preview') { throw 'Unexpected executable version.' }
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { throw 'Expected x64 PE.' }
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
$wide = [Text.Encoding]::Unicode.GetString($bytes)
foreach ($marker in @('SAT_CONFIG_DIR','SAT_TEST_MODE','SAT_TEST_GDI_TRACE','--mock-text','--mock-ocr','--self-test-ui','--stability-test-ui','SAT/1:mock_ocr','stability-fixture-key','MOCK_TLS_ONLY_KEY')) {
    if ($ascii.Contains($marker) -or $wide.Contains($marker)) { throw "Forbidden production test marker: $marker" }
}
$signature = Get-AuthenticodeSignature -LiteralPath $exe
if ($signature.Status -ne 'NotSigned') { throw 'Update release documentation for the actual signature state before packaging.' }
$release = Join-Path $root 'release'
$package = Join-Path $release 'Translator-v0.9.0-preview-Win10-x64'
$archive = $package + '.zip'
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $archive)) { throw 'Candidate already exists; do not overwrite historical packages.' }
$null = New-Item -ItemType Directory -Force -Path $release
$null = New-Item -ItemType Directory -Path $package
# Only explicit payload files; no directory copy from a build or user profile.
Copy-Item -LiteralPath $exe -Destination (Join-Path $package 'Translator.exe')
foreach ($name in @('RELEASE_NOTES.md','LICENSE','NOTICE','config.example.json')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package
}
Copy-Item -LiteralPath (Join-Path $root 'docs/THIRD_PARTY_NOTICES.md') -Destination $package
@'
# Snipaste AI Translator v0.9.0-preview 使用说明

这是本地诊断候选，不是稳定版，也尚未获准公开 Pre-release。
Win10 x64 是当前自动化环境；Win11／干净机／真人输入法与物理多屏尚未验证。
EXE 未签名。解压到可写目录，直接运行 Translator.exe，通常无需管理员权限。

打开设置 → 模型与 API，输入自己的 HTTPS 服务地址、准确模型 ID 和 API Key。
密钥由当前用户 DPAPI 加密保存，不要发给他人。config.example.json 没有密钥，
仅展示 schema；不要覆盖既有配置。测试连接会消耗 API 用量。

Ctrl+V 载入文字，选择目标语言。启用自动翻译时采用输入可立即发送，关闭时点击翻译。
解锁原文后可以编辑；Ctrl+Enter 提交。支持五种改写、自然化、自定义三范围指令和版本回退。
版本只在会话内保存；失败或取消不会成为完整成功版本。
思考模式强制开关只适配代码所识别的 DeepSeek 官方端点及 deepseek-flash，其他用自动。

OCR 需要用户自己的 Snipaste 专业版桌面安装和授权，先启动，再在常规设置指定准确 EXE。
建议开启候选确认，核对文字后采用，并关闭敏感内容的自动翻译。
当前调用 snip -o ocr_clipboard，剪贴板变化不能证明来源；真实 OCR 仍未验收。
Esc 取消本工具等待，不结束已有 Snipaste。OCR→exec 一步联动仍 HOLD。
粘贴翻译不依赖 Snipaste。其他 OCR 来源尚未实现。

已知阻断：GUI 严格资源零增长门失败；不要把本候选当成稳定生产版。
完整测试统计见 PREVIEW.txt / BUILD_INFO.json；依赖见 DEPENDENCIES.txt。
云端 Provider／真人 IME／物理 DPI／Win11／干净机及远程 CI 仍 UNVERIFIED。
Apache-2.0 许可见 LICENSE / NOTICE；资源与工具链条件见 THIRD_PARTY_NOTICES.md。

关闭最后窗口后本工具退出，用户的 OCR 软件可能独立常驻。
卸载：关闭程序，删除解压目录。配置仍在 %LOCALAPPDATA%/SnipasteAITranslator；
仅明确希望删除自己的数据时才手动删除该目录。不要分享该配置或 secret-*.bin。
反馈通过所有者既有渠道，附系统版本、EXE hash、脱敏步骤，勿附 OCR 内容或 Key。

包内 SHA256SUMS.txt 校验有效载荷；ZIP 自身的哈希在包外同名校验清单。
'@ | Set-Content -LiteralPath (Join-Path $package 'USER_GUIDE.zh-CN.md') -Encoding utf8
@"
v0.9.0-preview — LOCAL RELEASE CANDIDATE ONLY
Stable: NO-GO. Public pre-release: NO-GO until release gates are closed.
Current CTest: $passed PASS / $failed FAIL / $skipped SKIP.
Unsigned binary; real OCR, cloud Provider, human IME, physical mixed DPI,
Windows 11, clean-machine and hosted CI acceptance remain UNVERIFIED.
No credentials, user configuration, test tools or internal logs are included.
Read USER_GUIDE.zh-CN.md and RELEASE_NOTES.md. Full readiness is in the source docs.
"@ | Set-Content -LiteralPath (Join-Path $package 'PREVIEW.txt') -Encoding utf8
$dumpbin = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/2022') -Filter dumpbin.exe -Recurse -File |
    Where-Object { $_.FullName -match 'Hostx64[\\/]x64[\\/]' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin x64 is required for dependency evidence.' }
$depOutput = & $dumpbin.FullName /DEPENDENTS $exe
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed.' }
$dependencies = @($depOutput | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] } })
$systemDlls = @('USER32.dll','GDI32.dll','bcrypt.dll','SHELL32.dll','ADVAPI32.dll','dwmapi.dll','CRYPT32.dll','WINHTTP.dll','COMCTL32.dll','ole32.dll','IMM32.dll','COMDLG32.dll','d2d1.dll','KERNEL32.dll','SHLWAPI.dll')
if ($dependencies.Count -lt 1 -or @($dependencies | Where-Object { $_ -notin $systemDlls }).Count -gt 0) { throw 'Unexpected runtime dependency; review before distribution.' }
$dependencies + 'Msftedit.dll (dynamic load from Windows System32)' |
    Set-Content -LiteralPath (Join-Path $package 'DEPENDENCIES.txt') -Encoding ascii
$cache = Get-Content -LiteralPath (Join-Path $root "$BuildDirectory/CMakeCache.txt")
$cmakeVersion = ($cache | Where-Object { $_ -match '^CMAKE_CACHE_(MAJOR|MINOR|PATCH)_VERSION:INTERNAL=' } | ForEach-Object { ($_ -split '=')[1] }) -join '.'
$clPath = Join-Path $dumpbin.DirectoryName 'cl.exe'
$compilerVersion = (Get-Item -LiteralPath $clPath).VersionInfo.FileVersion
$fileVersion = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
$sourceHashes = [ordered]@{}
foreach ($file in @((Get-Item -LiteralPath (Join-Path $root 'CMakeLists.txt'))) + @(Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests') -File -Recurse | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\','/')
    $sourceHashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
}
$info = [ordered]@{
    version = $version; numeric_version = '0.9.0.0'; file_version = $fileVersion
    platform = 'Windows x64, preview tested on Windows 10 build 19045'
    build = 'MSVC Release /MT C++20 /W4 /WX'; compiler_file_version = $compilerVersion
    cmake = $cmakeVersion; executable_bytes = $bytes.Length
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    signature = [string]$signature.Status; tests = @{ PASS=$passed; FAIL=$failed; SKIP=$skipped }
    junit_sha256 = (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash
    test_report_for_build = $BuildDirectory; dependencies = $dependencies
    system_dynamic_dependency = 'Msftedit.dll'; hosted_ci = 'UNVERIFIED'
    source_inventory_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'docs/PUBLIC_FILES.txt')).Hash
    build_source_hashes = $sourceHashes
    public_pre_release = 'NO-GO'; stable = 'NO-GO'; paid_provider_calls = 0
}
$info | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'BUILD_INFO.json') -Encoding utf8
Get-ChildItem -LiteralPath $package -File | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash + '  ' + $_.Name
} | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding ascii
Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
$unpack = Join-Path $root "$BuildDirectory/unpacked-v0.9.0-preview"
if (Test-Path -LiteralPath $unpack) { throw 'Unpack directory already exists.' }
Expand-Archive -LiteralPath $archive -DestinationPath $unpack
$unpacked = Join-Path $unpack (Split-Path -Leaf $package)
foreach ($file in Get-ChildItem -LiteralPath $package -File) {
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $unpacked $file.Name)).Hash) { throw 'Extracted file differs.' }
}
(Get-FileHash -LiteralPath $archive).Hash + '  ' + (Split-Path -Leaf $archive) |
    Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding ascii
$report = [ordered]@{version=$version; archive=Split-Path -Leaf $archive; archive_bytes=(Get-Item -LiteralPath $archive).Length; archive_sha256=(Get-FileHash -LiteralPath $archive).Hash; executable_sha256=$info.executable_sha256; executable_bytes=$bytes.Length; files=@(Get-ChildItem -LiteralPath $package -File | Select-Object -ExpandProperty Name); extraction_hashes_match=$true; machine='8664'; signature=[string]$signature.Status; dependencies=$dependencies; tests=$info.tests}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "$BuildDirectory/package-verification.json") -Encoding utf8
$report | ConvertTo-Json -Depth 5
