param(
    [string]$BuildDirectory = 'build/stable-v091',
    [string]$TestReport = 'build/stable-v091/ctest-release.xml',
    [string]$Version = '0.9.1',
    [string]$HostedCiStatus = 'UNVERIFIED',
    [string]$HostedCiRunUrl = '',
    [string]$LocalCtestSummary = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Version -notmatch '^\d+\.\d+\.\d+(-preview)?$') { throw 'Expected a semantic version with an optional -preview suffix.' }
$expectedVersion = $Version
$isStable = $expectedVersion -notmatch '-preview$'
if ($isStable -and ($HostedCiStatus -ne 'PASS' -or -not $HostedCiRunUrl)) { throw 'Stable packaging requires a passing hosted CI run URL.' }
$sourceCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
& git -C $root diff --quiet HEAD -- .
if ($LASTEXITCODE -ne 0) { throw 'Commit all tracked release changes before packaging.' }
if ($isStable) {
    if ($HostedCiRunUrl -notmatch '^https://github.com/rowanjove/OCR-AI-Translator/actions/runs/(\d+)$') { throw 'Unexpected hosted CI URL.' }
    $runId = $Matches[1]
    $ciJson = & gh run view $runId --repo rowanjove/OCR-AI-Translator --json headSha,status,conclusion,event
    if ($LASTEXITCODE -ne 0) { throw 'Cannot verify hosted CI.' }
    $ci = $ciJson | ConvertFrom-Json
    if ($ci.headSha -ne $sourceCommit -or $ci.status -ne 'completed' -or $ci.conclusion -ne 'success' -or $ci.event -ne 'push') { throw 'Hosted CI must pass for this exact source commit.' }
    $ciEvidence = Join-Path $root "$BuildDirectory/hosted-ci-$runId"
    & gh run download $runId --repo rowanjove/OCR-AI-Translator --name windows-x64-test-evidence --dir $ciEvidence
    if ($LASTEXITCODE -ne 0) { throw 'Cannot download hosted test evidence.' }
    [xml]$hostedXml = Get-Content -LiteralPath (Join-Path $ciEvidence 'ci/ctest.xml') -Raw
    $hostedCases = @($hostedXml.SelectNodes('//testcase'))
    if ($hostedCases.Count -ne 10 -or @($hostedXml.SelectNodes('//failure | //error | //skipped')).Count -ne 0 -or @($hostedCases | Where-Object { $_.name -eq 'native_clipboard_isolated' }).Count -ne 1) { throw 'Hosted CI must pass all 10 tests, including isolated clipboard.' }
}
$exe = Join-Path $root "$BuildDirectory/Release/Translator.exe"
$testPath = Join-Path $root $TestReport
[xml]$xml = Get-Content -LiteralPath $testPath -Raw
$cases = @($xml.SelectNodes('//testcase'))
$failed = 0
$skipped = 0
$skipNames = @()
foreach ($case in $cases) {
    if ($null -ne $case.SelectSingleNode('./failure | ./error')) { $failed++ }
    if ($null -ne $case.SelectSingleNode('./skipped')) { $skipped++; $skipNames += $case.name }
}
$passed = $cases.Count - $failed - $skipped
if ($cases.Count -ne 10) { throw "Expected all 10 registered CTest cases; got $($cases.Count)." }
if ($failed -ne 0) { throw "CTest has $failed failure(s); refusing to package." }
if ($skipped -gt 1 -or ($skipped -eq 1 -and $skipNames[0] -ne 'native_clipboard_isolated')) { throw "Only native_clipboard_isolated may SKIP; got $($skipNames -join ', ')." }

$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($version -ne $expectedVersion) { throw "Unexpected executable version: $version (expected $expectedVersion)." }
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
$packageName = "OCR-AI-Translator-v$expectedVersion-Win10-x64"
$package = Join-Path $release $packageName
$archive = "$package.zip"
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $archive)) { throw 'Candidate already exists; preserve historical packages and choose a new version.' }
$null = New-Item -ItemType Directory -Force -Path $release
$null = New-Item -ItemType Directory -Path $package
Copy-Item -LiteralPath $exe -Destination (Join-Path $package 'Translator.exe')
foreach ($name in @('RELEASE_NOTES.md','LICENSE','NOTICE','config.example.json')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package
}
Copy-Item -LiteralPath (Join-Path $root 'docs/THIRD_PARTY_NOTICES.md') -Destination $package

@"
# OCR AI Translator $expectedVersion 使用说明

$(if ($isStable) { "这是 $expectedVersion 稳定版。" } else { '这是预发布版。' }) 支持和验收平台为 Windows 10 x64。开发者确认真实 Snipaste OCR 和真实云 Provider 验收通过。Windows 11、干净机、物理 IME/DPI 和多显示器完整矩阵未验证。

EXE 未签名。解压到可写目录后直接运行 Translator.exe，通常无需管理员权限。打开设置 → 模型与 API，填写自己的 HTTPS 服务地址、准确模型 ID 和 API Key。密钥由当前用户 DPAPI 加密保存，不要发给他人。config.example.json 不含密钥，不要覆盖已有配置。

Ctrl+V 载入文字并选择目标语言。自动翻译默认关闭；开启后，采用输入可立即发送。纯文本粘贴翻译不依赖 Snipaste。OCR 需要用户自己的 Snipaste 专业版桌面安装和授权；在常规设置指定准确 EXE。OCR 候选确认默认开启，请核对文字后再采用。Esc 取消本工具的等待，不结束已有 Snipaste。

测试连接可能产生 API 用量。请勿分享 config.json、secret-*.bin、API Key 或私人 OCR 文本。关闭最后一个主窗口后本工具退出。配置保存在 `%LOCALAPPDATA%/SnipasteAITranslator`；卸载时只有在明确要删除个人数据的情况下才手动删除该目录。

许可为 Apache-2.0，依赖和工具链条件见 THIRD_PARTY_NOTICES.md。发布测试统计与已知限制见 RELEASE_NOTES.md 和 BUILD_INFO.json。
"@ | Set-Content -LiteralPath (Join-Path $package 'USER_GUIDE.zh-CN.md') -Encoding utf8

$ciDisplay = if ($HostedCiRunUrl) { $HostedCiRunUrl } else { 'No hosted CI run URL recorded.' }
$statusLabel = if ($isStable) { 'STABLE RELEASE' } else { 'STABILITY-FIX PREVIEW CANDIDATE' }
$acceptanceLabel = if ($isStable) { 'Release acceptance: GO.' } else { 'Candidate acceptance: GO for review. This is not a Stable release.' }
$localCtestDisplay = if ($LocalCtestSummary) { "Latest local CTest: $LocalCtestSummary" } else { 'No additional local CTest note recorded.' }
@"
OCR AI Translator $expectedVersion — $statusLabel
$acceptanceLabel
Packaged CTest report: $passed PASS / $failed FAIL / $skipped SKIP ($TestReport).
$localCtestDisplay
$(if ($skipped) { "Clipboard isolation SKIP; see the local CTest report for the environment error. The user's clipboard was not used." } else { 'All local CTest cases passed, including isolated clipboard.' })
Hosted CI status: $HostedCiStatus
$ciDisplay
The unchanged strict steady-state UI resource assertions passed. Bounded first-use resource changes are accepted; their precise object sources remain unidentified. See the release report for measured settings and render cycle results.
The user reports PASS for real Snipaste OCR and a real Provider; this is not independently reproduced in the release workflow. Windows 11, clean-machine, physical IME and mixed-DPI acceptance remain UNVERIFIED.
Unsigned x64 executable. No credentials, user configuration, OCR content, test tools or internal logs are included.
"@ | Set-Content -LiteralPath (Join-Path $package 'RELEASE_STATUS.txt') -Encoding utf8

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
    version = $version; numeric_version = if ($expectedVersion -match '-preview$') { $expectedVersion -replace '-preview$', '.0' } else { "$expectedVersion.0" }; file_version = $fileVersion
    source_commit = $sourceCommit; platform = 'Windows x64, tested on Windows 10 build 19045'
    build = 'MSVC Release /MT C++20 /W4 /WX'; compiler_file_version = $compilerVersion
    cmake = $cmakeVersion; executable_bytes = $bytes.Length
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    signature = [string]$signature.Status; tests = @{ PASS=$passed; FAIL=$failed; SKIP=$skipped }
    junit_sha256 = (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash
    test_report = $TestReport; local_ctest_note = $LocalCtestSummary; dependencies = $dependencies
    system_dynamic_dependency = 'Msftedit.dll'; hosted_ci_status = $HostedCiStatus; hosted_ci_run_url = $HostedCiRunUrl
    source_inventory_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'docs/PUBLIC_FILES.txt')).Hash
    build_source_hashes = $sourceHashes; stable = if ($isStable) { 'STABLE' } else { 'NOT_STABLE' }; automated_paid_provider_calls = 0
}
$info | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'BUILD_INFO.json') -Encoding utf8
Get-ChildItem -LiteralPath $package -File | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash + '  ' + $_.Name
} | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding ascii

Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
$unpack = Join-Path $root "$BuildDirectory/unpacked-$expectedVersion"
if (Test-Path -LiteralPath $unpack) { throw 'Unpack directory already exists; do not overwrite package evidence.' }
Expand-Archive -LiteralPath $archive -DestinationPath $unpack
$unpacked = Join-Path $unpack $packageName
foreach ($file in Get-ChildItem -LiteralPath $package -File) {
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $unpacked $file.Name)).Hash) { throw "Extracted file differs: $($file.Name)." }
}
(Get-FileHash -LiteralPath $archive).Hash + '  ' + (Split-Path -Leaf $archive) |
    Set-Content -LiteralPath (Join-Path $release "$packageName-SHA256SUMS.txt") -Encoding ascii
$report = [ordered]@{
    version=$version; archive=Split-Path -Leaf $archive; archive_bytes=(Get-Item -LiteralPath $archive).Length
    archive_sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    executable_sha256=$info.executable_sha256; executable_bytes=$bytes.Length
    files=@(Get-ChildItem -LiteralPath $package -File | Select-Object -ExpandProperty Name)
    extraction_hashes_match=$true; machine='8664'; signature=[string]$signature.Status
    dependencies=$dependencies; tests=$info.tests; source_commit=$sourceCommit
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "$BuildDirectory/package-verification.json") -Encoding utf8
$report | ConvertTo-Json -Depth 5
