# Testing v0.9.0-preview

Local result on 2026-10-14, Windows 10 x64 build 19045: **8 PASS / 1 FAIL /
1 SKIP**, 96.09 seconds. Release build passed with MSVC 19.44.35226 and
Windows SDK 10.0.26100.0, `/W4 /WX`. Test exit code: 8; it is not a green suite.

| Test | Result | Seconds |
| --- | --- | --- |
| `ui_layout_geometry` | PASS | 0.140217 |
| `probe_unit` | PASS | 0.0658627 |
| `native_clipboard_isolated` | SKIP | 0.0704242 |
| `core_tests` | PASS | 15.7131 |
| `process_lifecycle` | PASS | 20.4564 |
| `translator_gui_smoke` | PASS | 13.8043 |
| `capture_window_lifecycle` | PASS | 6.52333 |
| `ui_stability_faults` | FAIL | 28.5 |
| `network_stability_faults` | PASS | 0.497229 |
| `process_stability_faults` | PASS | 10.3044 |

The GUI stability resource assertion remains unchanged and failed. No tolerance
was widened and no failure was converted to a skip. Private clipboard window
station creation failed with Win32 5; return 77 means SKIP, not PASS. There is
no fallback that writes to the user's clipboard.

## Reproduction

```powershell
./scripts/build.ps1 -BuildDirectory build/local
# Individual steps (VS developer environment):
cmake -S . -B build/local -G "Visual Studio 17 2022" -A x64
cmake --build build/local --config Release --parallel 2
ctest --test-dir build/local -C Release --output-on-failure --output-junit "$PWD/build/local/ctest.xml"
ctest --test-dir build/local -C Release -R '^capture_window_lifecycle$' --output-on-failure
# Optional narrower subset; passing this does not mean the full suite passed:
ctest --test-dir build/local -C Release -L headless --output-on-failure
```

Run tests through CTest. It injects distinct SAT_TEST_MODE/SAT_CONFIG_DIR
sandboxes into test-only targets; never point them at a user configuration.
Resource locks prevent test instances from colliding. The production EXE does
not recognize those overrides or Mock CLI inputs. No cloud keys, billable
requests, licensed Snipaste, or external model download are required.

Core/network tests use local HTTP fixtures with real WinHTTP; DPAPI tests use
fictional credentials and real local encryption. GUI/process/capture tests use
real HWNDs plus injected events or a fake Snipaste launcher. They do not perform
real OCR or human input. The same complete suite runs in Windows x64 CI; hosted
Runner behavior remains UNVERIFIED until an authorized remote run. Clipboard
isolation may SKIP depending on access. If HWND tests fail on the runner, retain
that failure and diagnose it rather than suppressing it.

## Release gates

Still UNVERIFIED: final-binary cloud Provider/TLS/proxy/billing, real professional
Snipaste OCR and clipboard provenance, human IME, physical mixed DPI/multi-screen,
Windows 11, accessibility and clean-machine installation/long-running behavior.
Prior local ASan evidence was not rerun in this packaging task and is not a new
instrumented pass. Stable remains NO-GO. See GITHUB_RELEASE_READINESS.md.

Current production screenshot launches preserve user config byte-for-byte and
leave no owned process; PrintWindow images are actual UI appearance evidence
only. Original raw build/JUnit/metrics/capture logs remain locally under ignored
`out/github-readiness` and `build/github-preview`; public documents summarize
results without private paths or content. CI uploads only its newly created
build/test evidence, never these historical local directories.
