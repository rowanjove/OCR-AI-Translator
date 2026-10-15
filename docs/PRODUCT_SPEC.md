# Product scope for 0.9.1

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

Windows 10 x64 is the supported and accepted platform. Real Snipaste OCR and
live cloud Provider acceptance are PASS according to the developer. This task's
automated evidence uses isolated fixtures and is recorded separately. Windows 11,
human IME, physical mixed DPI/multiple monitors and clean-machine coverage remain
unverified and are outside this release's acceptance scope. The EXE is unsigned.
Strict steady-state resource assertions remain required; bounded first-use
initialization changes alone do not block release.

The original detailed 2026-10-12 PRD and historical audit/handoff documents are
preserved locally and excluded from the public inventory because they include
obsolete status, local paths and private evidence references. This document is
the public current-scope summary; source and tests determine actual behavior.
