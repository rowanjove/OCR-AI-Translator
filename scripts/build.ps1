param([string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio 2022 Build Tools: Desktop development with C++ and Windows SDK.' }
$installation = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC C++ x64 toolset not found.' }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'Install CMake (or the Visual Studio CMake tools component).' }
$buildPath = Join-Path $projectRoot $BuildDirectory
& $cmakePath -S $projectRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakePath --build $buildPath --config Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildPath -C Release --output-on-failure --output-junit (Join-Path $buildPath 'ctest-release.xml')
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
[xml]$results = Get-Content -LiteralPath (Join-Path $buildPath 'ctest-release.xml') -Raw
foreach ($test in $results.testsuite.testcase) {
    if ($null -ne $test.skipped) { Write-Warning "ENVIRONMENT SKIP (not passed): $($test.name). See ctest-release.xml and TEST_REPORT.md." }
}
Get-Item (Join-Path $buildPath 'Release\Probe.exe'), (Join-Path $buildPath 'Release\SpikeRunner.exe'), (Join-Path $buildPath 'Release\Translator.exe') | Select-Object FullName,Length
