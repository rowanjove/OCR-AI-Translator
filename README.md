# Snipaste AI Translator

[简体中文](README.zh-CN.md) · **v0.9.0-preview** · Apache-2.0

An extremely lightweight native Windows translation and text-processing tool
with extensible OCR inputs and custom AI APIs. Paste translation works
independently of OCR. Snipaste is the only implemented OCR adapter; this is an
independent project, not an official Snipaste plugin. Other OCR sources are plans.

**Local release candidate only. Stable release: NO-GO.** The strict GUI resource
stability gate has an unresolved failure. Real OCR, the final binary's cloud API,
human IME, physical mixed DPI, Windows 11 and clean-machine acceptance remain
UNVERIFIED. See [release readiness](docs/GITHUB_RELEASE_READINESS.md).

## Actual interface

![Light theme](docs/screenshots/light.png)
![Dark theme](docs/screenshots/dark.png)

Actual production windows captured with PrintWindow, empty source/result and no
API request. These are UI evidence, not a demonstration of successful translation.

## Features

- Native C++20 / Win32, RichEdit and Windows networking/crypto; static CRT.
  No Electron, Qt, WebView2, Python runtime, bundled model or database.
- Streaming translation, target-language selection, source unlock/edit/restore.
- Five rewrite styles: natural, concise, casual, formal and literary;
  naturalization (no promise of bypassing AI detection).
- Custom instructions with source+result / result-only / source-only scopes;
  2,000 UTF-16-code-unit limit and optional session prompt memory.
- Session-only completed versions: default 10, configurable 1–50, with rollback.
- Multiple API profiles and separate translation/rewrite role bindings.
- Light/dark/system theme, toolbar visibility/order and narrow-window overflow.
- Explicit Snipaste executable path, capture timeout/cancellation and optional
  candidate confirmation. Clipboard changes do not prove OCR provenance.

Translator exits with its final window. Your separately installed OCR tool or
local model service may continue running under its own lifecycle.

## Download, run and uninstall

Source repository: [GitHub](https://github.com/rowanjove/Snipaste-AI-Translator). Source-only publication is authorized;
no binary release is published. A GitHub pre-release requires separate approval
after resolving the documented gates.
The local candidate is `release/Translator-v0.9.0-preview-Win10-x64.zip`;
verify the adjacent `SHA256SUMS.txt` before extracting to a writable folder.
Run `Translator.exe`; no installer/admin rights are normally needed.
The binary is **unsigned** and is not represented as Windows code-signing certified.

Current local build/test environment: Windows 10 x64 build 19045, VS2022/MSVC
and Windows SDK. Windows 10 x64 is the preview target; Windows 11 x64 is planned
and UNVERIFIED. No x86, ARM64, Linux or macOS build is supplied. A compatible
AI service/account/network is needed for actual AI operations; Snipaste
professional desktop edition is needed only for its OCR adapter.

Close the app and remove its extracted folder to uninstall. Settings/DPAPI
credentials remain in `%LOCALAPPDATA%/SnipasteAITranslator`; remove that folder
only if you deliberately want to erase your configuration. This preparation does
not delete it, unregister your OCR tool or revoke any API key.

## Paste and screenshot translation

1. Open Translator, paste text with Ctrl+V and select the target language.
2. Configure an API profile in Settings → Model & API. If automatic translation
   is enabled, accepting new input can send it immediately; otherwise click
   Translate. Unlock source to edit; Ctrl+Enter submits the edited text.
3. For OCR, start your licensed Snipaste professional desktop installation,
   then configure its exact `.exe` path in General settings. The default is
   `C:\Tools\Snipaste\Snipaste.exe`; there is no WindowsApps/PATH fallback.
4. Enable **candidate confirmation** and inspect captured text before adopting
   it. Consider disabling automatic translation for sensitive input. Click
   screenshot translation or launch `Translator.exe --snip`; Esc cancels this
   tool's wait. It does not terminate your existing Snipaste process.

The adapter invokes `snip -o ocr_clipboard`; OCR may alter the system clipboard.
Real OCR integration remains UNVERIFIED. `--from-snipaste-ocr` is on HOLD;
the combined OCR→exec workflow is not advertised as working. You can instead
complete OCR yourself and launch `Translator.exe --clipboard-text`.

```powershell
.\Translator.exe --clipboard-text
.\Translator.exe --text-file 'C:\Temp\sample.txt'
.\Translator.exe --settings
.\Translator.exe --provider-check translation | Out-String
```

`--text-file` reads UTF-8 with optional BOM. Offline diagnostics do not call a
provider. The explicitly selected connection test or `--provider-check-online`
can incur API charges. Production binaries do not expose Mock/test input hooks.

## Custom AI API and thinking mode

Enter an HTTPS Base URL, provider's exact model ID and your API Key in settings.
For documentation only: `https://api.example.invalid/v1`, `example-model`,
`PLACEHOLDER_NOT_A_KEY`. These values cannot authenticate or translate.
`config.example.json` is a schema example with no key and an empty credential
reference; do not overwrite your existing user config with it. Enter real keys
only in the local settings UI; they are saved through current-user Windows DPAPI.

Implemented protocol: OpenAI Chat Completions-compatible `/chat/completions`,
SSE `delta.content` and non-streaming `message.content`. An explicit unsupported
stream response allows one non-streaming fallback. No general compatibility
claim is made for all services. Responses API and native Anthropic/Gemini
protocols are unsupported. HTTPS is required except loopback HTTP; the app
connects to an existing local service but never installs or starts one.

Thinking can be automatic/disabled/enabled per profile. Forced switches are
implemented only for the recognized official DeepSeek endpoint and exact
`deepseek-flash` model; other configurations must use automatic. Automatic
omits the vendor switch. New recognized DeepSeek profiles default to disabled.
Provider availability and final-binary online behavior are UNVERIFIED. See
[API compatibility](docs/API_COMPATIBILITY.md) for the exact source contract.

## Build and test

Install Visual Studio 2022 / Build Tools with **Desktop development with C++**,
MSVC x64, Windows SDK and CMake >= 3.20. No third-party C++ download is needed.
Clone the repository, open PowerShell at its root, then run:

```powershell
git clone https://github.com/rowanjove/Snipaste-AI-Translator.git
cd Snipaste-AI-Translator
.\scripts\build.ps1 -BuildDirectory build/local
# Or use cmake/ctest from the VS developer environment:
cmake -S . -B build/local -G 'Visual Studio 17 2022' -A x64
cmake --build build/local --config Release --parallel 2
ctest --test-dir build/local -C Release --output-on-failure
```

Production output: `build/local/Release/Translator.exe`. `/W4 /WX` makes warnings
fail the build. A known stability failure can make the all-in-one script return
an error **after successful compilation**; do not remove assertions to obtain
a green build. Test executables are development tools, not release payloads.

[CI workflow](.github/workflows/build.yml) uses Windows x64, fictional local
fixtures and all registered CTest tests. It does not call paid cloud models or
require Snipaste licensing. A skipped isolated clipboard test is not a pass;
GUI fixture tests do not represent human/OCR acceptance. The first hosted
GitHub run is checked separately from local results. CI artifacts contain only
test evidence, not executable downloads. First hosted run: [9 PASS / 1 FAIL / 0 SKIP](https://github.com/rowanjove/Snipaste-AI-Translator/actions/runs/38578032811) (settings warmup
assertion failed). Details: [testing](docs/TESTING.md).

## Privacy, limitations and roadmap

AI operations send selected source/result text to your configured service.
Provider retention/billing is separate. Session text/versions are not persisted
by default; the tool has no telemetry, update service or persistent clipboard
monitor. DPAPI protects at rest for the Windows user, not against software
already running as that user. Do not share config, encrypted secret blobs,
OCR content or HTTP dumps. Read [security](SECURITY.md).

Before public binary release: resolve the resource-stability failure, confirm
MSVC redistribution rights, run hosted CI and real-device acceptance. Additional
OCR adapters, screenshot-file OCR, richer protocol drivers and accessibility
acceptance are future work. Native geometry/Mock passes are not physical DPI,
Windows 11 or real-provider acceptance.

## Contributing and license

See [contributing](CONTRIBUTING.md), [architecture](docs/ARCHITECTURE.md) and
[changelog](CHANGELOG.md). Report reproducible bugs through [GitHub Issues](https://github.com/rowanjove/Snipaste-AI-Translator/issues).
Never include secrets or private text.

Apache License 2.0: [LICENSE](LICENSE), [NOTICE](NOTICE) and
[resource/dependency review](docs/THIRD_PARTY_NOTICES.md). The owner selected this
license and confirmed original icon rights. This is not a blanket legal
certification of all historical code or Windows toolchain redistribution.
