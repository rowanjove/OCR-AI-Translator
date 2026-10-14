# Product scope for 0.9.0-preview

OCR AI Translator is an extremely lightweight native Windows translation
and text-processing tool with extensible OCR inputs and custom AI APIs. The
existing name and icon are retained; it is an independent project.

Paste translation is a core capability independent of OCR. The only implemented
OCR adapter launches an explicitly configured Snipaste professional desktop
EXE with `snip -o ocr_clipboard`. The combined OCR-to-exec automatic workflow
is on HOLD. Screenshot-file OCR and additional OCR sources are plans.

Native C++20 / Win32, no Qt, Electron, WebView2, model package or runtime service.
The application exits when its final window closes. A user's separately running
OCR tool or model service has its own process lifecycle.

Implemented capabilities: streaming translation, source editing, target-language
selection, five rewrite presets, naturalization, custom instructions, session
versions, multiple provider profiles and translation/rewrite role binding.
Naturalization does not promise to bypass AI detection. Structured prompts do
not guarantee immunity to model prompt injection.

Quality targets must be measured, never treated as claims. Windows 10 x64 is the
current local automation environment; Windows 11, real OCR, cloud-provider,
human IME, physical mixed DPI and clean-machine acceptance remain unverified.
A PREVIEW with a failing resource-stability test is not a stable release.

The original detailed 2026-10-12 PRD and historical audit/handoff documents are
preserved locally and excluded from the public inventory because they include
obsolete status, local paths and private evidence references. This document is
the public current-scope summary; source and tests determine actual behavior.
