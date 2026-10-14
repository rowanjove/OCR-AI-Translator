# OCR AI Translator v0.9.1-preview

v0.9.1-preview is a stability-fix candidate, not a Stable release.

## Download and verify

- Download `OCR-AI-Translator-v0.9.1-preview-Win10-x64.zip` from this release.
- Verify it with the adjacent `OCR-AI-Translator-v0.9.1-preview-SHA256SUMS.txt` file, then extract and run `Translator.exe`.
- The executable is unsigned. No installer or administrator rights are normally required.

## Fixes

- Saving Provider settings and using paste translation no longer depends on Snipaste being installed. Starting OCR still validates the configured Snipaste executable and reports a clear error when it is unavailable.
- New configurations and missing preference fields now default to automatic translation off and OCR candidate confirmation on. Explicit values in existing configurations remain unchanged.
- The GUI stability test now uses a deterministic missing Snipaste path and explicitly warms the first settings open/theme preview after final quiescence. Its strict 50-cycle settings and 100-cycle render resource assertions remain unchanged.

## Validation

- Windows 10 x64 Release build: PASS.
- Full local CTest: **9 PASS / 0 FAIL / 1 SKIP**. `native_clipboard_isolated` skipped because this host could not create a private window station (Win32 error 5); it did not access the user's clipboard.
- `ui_stability_faults`: PASS in three consecutive runs and in the full suite. First-use settings warmup added one GDI object; its source is unidentified. The measured 50 settings and 100 render cycles showed no resource growth. One process handle was also added during render warmup; its object type remains unidentified.
- GitHub Actions for this candidate: see the run linked from the release after the source push.

## Known limits

The full Windows 11, clean-machine, real Snipaste OCR/clipboard, live cloud Provider, physical IME and mixed-DPI matrix has not been completed. Those items remain UNVERIFIED. This package is a preview candidate and does not claim Stable release readiness.

Paste translation is independent of OCR. Snipaste professional desktop software and its own authorization are required for OCR. The package contains no API credentials, user configuration, DPAPI data, private OCR text, test executables or internal logs.

See `USER_GUIDE.zh-CN.md`, `BUILD_INFO.json` and `DEPENDENCIES.txt` for use, build evidence and runtime dependencies. The source repository contains the full testing record.
