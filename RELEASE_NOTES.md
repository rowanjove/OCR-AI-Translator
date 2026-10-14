# v0.9.0-preview — release notes draft

**Unpublished local candidate. Public Pre-release: NO-GO; Stable: NO-GO.**
This is a diagnostic preview with an unresolved GUI resource-stability gate.
The owner approved source-only publication on 2026-10-14. No binary release
or candidate ZIP/EXE upload is authorized.

Snipaste AI Translator is an extremely lightweight native Windows translation
and text-processing tool with extensible OCR sources and custom AI APIs.
Paste translation works independently of OCR. Snipaste is the only implemented
OCR adapter, still awaiting real integration acceptance. This is an independent
project and is not an official Snipaste plugin.

## Features

- Native C++20/Win32 two-pane text UI, light/dark/system theme and source editing.
- OpenAI Chat Completions-compatible streaming/JSON, multiple API profiles and
  separate translation/rewrite roles.
- Five rewrite styles, naturalization, scoped custom instructions and session
  version rollback. Naturalization does not promise to evade AI detection.
- Per-profile thinking mode; forced switch only for the exact recognized
  official DeepSeek endpoint/model. Other configurations use automatic.
- Explicit-path Snipaste capture, bounded waiting and candidate confirmation.
  OCR→exec one-step integration remains HOLD; other OCR sources are plans.

## Install and verify

Candidate file: `Translator-v0.9.0-preview-Win10-x64.zip`. Verify the adjacent
SHA256SUMS.txt, extract to a writable folder and run Translator.exe. Configure
an HTTPS service, exact model ID and your API key locally. Paste text to begin.
For OCR, separately install/license/start Snipaste professional desktop edition
and configure its exact EXE path. Enable candidate confirmation and check input.
No installer or bundled model is supplied. The EXE is unsigned.

Windows 10 x64 build 19045 is the current tested environment; Windows 11 and
clean-machine acceptance remain UNVERIFIED. Windows system DLLs and RichEdit
are required, with static CRT; no Qt/Electron/WebView2/Python runtime dependency.
The separately running OCR tool has its own background process.

## Actual test state and limits

Release x64 build: PASS. Full local CTest: **8 PASS / 1 FAIL / 1 SKIP**, 96.09 s.
`ui_stability_faults` fails the strict GDI/USER/kernel resource-growth assertion.
`native_clipboard_isolated` is environment SKIP (Win32 5), not a pass.
All other registered local fixtures passed. Build/Mock/native automation do
not establish live OCR, cloud-provider, human IME or physical multi-DPI success.
Hosted GitHub CI has not run. The package contains no real keys, user config,
DPAPI ciphertext, private text, test executable, internal log or build cache.

Before any public binary pre-release, resolve or explicitly reassess the failed
stability gate, verify hosted CI and applicable MSVC redistribution rights, and
record corresponding real-device acceptance. This draft is not stable-release
approval. Source-only publication can be separately approved after owner review.

Apache-2.0 was selected by the owner; existing icon rights were confirmed.
See LICENSE, NOTICE, THIRD_PARTY_NOTICES.md and the readiness report.
To uninstall, close and remove the extracted directory; local settings remain
until deliberately removed by the user.

## Feedback

Report sanitized reproducible steps, OS and binary hash through
[GitHub Issues](https://github.com/rowanjove/Snipaste-AI-Translator/issues). Never attach API keys,
DPAPI files, user configuration, private OCR/translation content or HTTP dumps.
