# UI behavior

- Two native text panes. Source is read-only until unlocked; editing expands it
  and hides the result. Ctrl+Enter submits; restore returns the input snapshot.
- Translation language changes act on source text. Failed operations restore a
  matching completed version; only complete results enter history.
- Five rewrite presets, naturalization and a custom instruction panel with
  source+result / result-only / source-only scopes. Instruction limit: 2,000
  UTF-16 code units. Prompt memory is session-only and can be disabled.
- Version capacity defaults to 10 and is configurable from 1 to 50.
- Single toolbar row, overflow menu for narrow windows, fixed settings access,
  theme/font/layout changes and independent settings drafts.
- Per-monitor-v2 manifest and DPI-aware geometry are implemented. Native HWND
  checks do not substitute for physical multi-monitor or human IME acceptance.
- Esc closes overlays or cancels capture/edit according to focus. Ctrl+V loads
  clipboard input outside normal text editing; native editing shortcuts remain.
- Clipboard OCR provenance is unknown. Enable candidate confirmation, inspect
  input before adopting it, and consider disabling automatic translation.

Public screenshots are actual production HWND PrintWindow captures with empty
source/result. They show UI appearance, not successful translation or a live OCR
flow. Desktop composition, Narrator, physical mixed DPI and human input remain
UNVERIFIED. The strict resource-stability gate is still a release blocker; see
[readiness](GITHUB_RELEASE_READINESS.md).
