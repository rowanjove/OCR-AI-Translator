# OCR AI Translator

[简体中文](README.md) · **v0.9.1-preview** · [Download](https://github.com/rowanjove/OCR-AI-Translator/releases/tag/v0.9.1-preview) · Apache-2.0

A lightweight native Windows translation and text-processing tool with custom
AI APIs and extensible OCR inputs. Paste text directly, or use the currently
implemented Snipaste adapter. Additional OCR tools and an in-app OCR backend
are future plans, not current support claims.

## Interface

![Light theme](docs/screenshots/light.png)
![Dark theme](docs/screenshots/dark.png)

Actual production windows, with no fabricated translation output.

## Features

- Streaming translation, target-language selection and source unlock/edit/restore.
- Five rewrite styles: natural, concise, casual, formal and literary;
  naturalization does not promise to evade AI detection.
- Custom instructions with source+result / result-only / source-only scopes,
  limited to 2,000 UTF-16 code units.
- Session-only completed version history: default 10, configurable 1–50.
- Multiple API profiles and separate translation/rewrite roles; per-profile
  thinking settings subject to protocol compatibility.
- Light/dark/system theme, toolbar order/visibility and narrow-window overflow.
- C++20 / Win32, native RichEdit, WinHTTP and DPAPI; no Electron, Qt, WebView2
  runtime, bundled model or database.

The app exits with its last window. Your OCR or local model service can run independently.

## Download and use

Download **OCR-AI-Translator-v0.9.1-preview-Win10-x64.zip** from
[Releases](https://github.com/rowanjove/OCR-AI-Translator/releases), verify the adjacent checksum file, extract to a
writable directory and run `Translator.exe`. Administrator rights are normally
unnecessary. This is an **unsigned stability-fix preview candidate**; one
clipboard-isolation test is skipped due to local permissions, as disclosed in
the Release Notes and testing documentation.

Locally verified environment: Windows 10 x64 build 19045. Windows 11, physical
mixed DPI/multiple monitors, clean-machine and full human IME acceptance remain
unverified. No x86, ARM64, Linux or macOS build is supplied.

Open Settings → Model & API, enter your HTTPS endpoint, exact model ID and API key.
Paste with Ctrl+V and choose a language. Automatic translation can send newly
accepted input immediately; otherwise click Translate. Unlock source to edit
and submit with Ctrl+Enter. Connection tests can incur API charges; offline
`--diagnose` and `--provider-check` do not send requests.

```powershell
.\Translator.exe --clipboard-text
.\Translator.exe --text-file 'C:\Temp\sample.txt'
.\Translator.exe --settings
.\Translator.exe --provider-check translation | Out-String
```

Text files support UTF-8/BOM. To uninstall, close the app and remove its folder.
Settings remain under `%LOCALAPPDATA%/SnipasteAITranslator`, the legacy compatible
path. Remove that directory only if you deliberately want to erase your data.

## Current OCR adapter: Snipaste

Separately install, license and start Snipaste professional desktop edition.
Configure its exact EXE path in General settings, for example
`C:\Tools\Snipaste\Snipaste.exe`; there is no PATH or WindowsApps fallback.
This is an independent app, not an official Snipaste plugin.

Click screenshot translation or run `Translator.exe --snip`. The adapter invokes
`snip -o ocr_clipboard`. Enable candidate confirmation and inspect the text before
adopting it; disable automatic translation for sensitive input. Esc cancels this
app's wait without terminating an existing Snipaste process. OCR changes the
clipboard; a sequence change alone cannot authenticate its source.

Full real OCR acceptance is incomplete. `--from-snipaste-ocr` / the combined
OCR→exec workflow remains on HOLD. You can perform OCR yourself, then use
`--clipboard-text`. Paste translation requires no OCR tool.

## API and thinking settings

OpenAI Chat Completions-compatible `/chat/completions`, SSE `delta.content` and
JSON `message.content` are implemented. Explicit unsupported-stream responses
allow one non-streaming fallback. Compatibility with every service is not guaranteed.
Responses API, native Anthropic/Gemini protocols and vision OCR are unsupported.
Remote endpoints use HTTPS; loopback HTTP can connect to an existing local
service, which this app never installs or starts.

Fictional examples: `https://api.example.invalid/v1`, `example-model`,
`PLACEHOLDER_NOT_A_KEY`. The config example has no key and an empty credential
reference. Do not overwrite your user config; enter real keys only locally,
where current-user Windows DPAPI protects stored credentials.

Forced thinking enabled/disabled is implemented only for the recognized official
DeepSeek endpoint and exact `deepseek-flash` model. Other configurations use
automatic, which omits vendor-specific switches. New recognized DeepSeek profiles
default to disabled. See [API compatibility](docs/API_COMPATIBILITY.md).

## Build and test

Visual Studio 2022 / Build Tools with C++ desktop tools, MSVC x64, Windows SDK and
CMake >= 3.20. No third-party C++ library download is required.

```powershell
git clone https://github.com/rowanjove/OCR-AI-Translator.git
cd OCR-AI-Translator
.\scripts\build.ps1 -BuildDirectory build/local
# Or from the VS developer environment:
cmake -S . -B build/local -G 'Visual Studio 17 2022' -A x64
cmake --build build/local --config Release --parallel 2
ctest --test-dir build/local -C Release --output-on-failure
```

Output: `build/local/Release/Translator.exe`. `/W4 /WX` treats warnings as errors.
Known GUI stress failures can make the all-in-one script fail after successful
compilation. Assertions remain intact. CI uses fictional local fixtures, not
paid cloud APIs. Mock/native HWND tests are not human OCR/IME/physical DPI acceptance.
See [testing](docs/TESTING.md) and [release record](docs/GITHUB_RELEASE_READINESS.md).

## Privacy and roadmap

AI operations send selected source/result text to your configured service;
provider retention/billing policies are separate. Session text/history is not
persisted by default. No telemetry, update service or persistent clipboard monitor.
DPAPI does not protect against software running as the same Windows user.
Never share config, encrypted secret blobs, private screenshots/text or HTTP dumps.
See [security](SECURITY.md).

Planned: more external OCR adapters, feasibility work on built-in OCR, additional
API drivers, accessibility and real-device compatibility acceptance.

## Contributing and license

Use [GitHub Issues](https://github.com/rowanjove/OCR-AI-Translator/issues) with a sanitized reproduction.
See [contributing](CONTRIBUTING.md), [architecture](docs/ARCHITECTURE.md) and
[changelog](CHANGELOG.md). [Apache-2.0](LICENSE); original icon rights confirmed
by the owner. [NOTICE](NOTICE) and [resource review](docs/THIRD_PARTY_NOTICES.md).
