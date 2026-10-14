# Testing v0.9.1-preview

## v0.9.1 stability-fix candidate (2026-10-15)

Windows 10 x64 build 19045, Visual Studio 2022/MSVC 17.14.40, Release
configuration: configure/build PASS; full CTest **9 PASS / 0 FAIL / 1 SKIP**
(108.16 seconds). The skipped
`native_clipboard_isolated` fixture could not create a private window station
(Win32 error 5); no test touched the user's clipboard. The JUnit report is
`build/v091/ctest-release.xml` in the local validation workspace.

`ui_stability_faults` passed in the full suite and in three consecutive repeat
runs (114.21 seconds total). After final quiescence, an explicit first-use
settings open/theme-preview/cancel warmup records a GDI count change from 56 to
57; its object source is unknown. The following strict 50-cycle settings phase
measured GDI 57→55, USER 67→64 and process handles 300→300. After render warmup,
the measured 100-cycle phase had process handles 301→301, GDI 57→57, USER 65→65,
and zero theme-brush deletion failures. One process handle was added during
render warmup (cycle 34 in this run); its object type is unknown, so its source
is not claimed.

Windows 11, clean-machine, real Snipaste OCR/clipboard, live cloud Provider and
physical IME/DPI remain separate UNVERIFIED matrix items. The v0.9.1 candidate
is GO for review; this does not certify a Stable release. Hosted CI status is
recorded after the push.

## Previous v0.9.0-preview evidence (2026-10-14)

### Original branded preview build

OCR AI Translator: Release x64 build PASS, full local CTest **8 PASS / 1 FAIL /
1 SKIP**, 98.87 seconds. GUI resource zero-growth assertion FAIL; clipboard
isolation SKIP due to local permissions. Business assertions remain intact.
Source names/metadata changed; the existing DPAPI entropy, config directory,
mutex and window class identifiers remain compatible with previous versions.
The owner explicitly authorized a public preview with these results disclosed.
This authorization does not convert failed/unverified tests into PASS.


## First hosted Windows CI (2026-10-14)

[Run 38578032811](https://github.com/rowanjove/OCR-AI-Translator/actions/runs/38578032811)
for source commit `5c86b249de3ffe60b98401bde2b323f53edcf62a`: audit/configure/Release
build PASS; full CTest **9 PASS / 1 FAIL / 0 SKIP**, 34.23 seconds, exit 8.
`native_clipboard_isolated` passed on the runner. `ui_stability_faults` failed
earlier with `settings warmup did not close`; it did not reach the resource
sampling gate. This is not proof of the same cause as the local resource failure.
The runner-specific root cause is UNVERIFIED. All assertions remain intact.
The workflow correctly concluded failure. Its artifact contains audit/JUnit/test
logs only, never an EXE. This hosted run does not establish human or Windows 11
acceptance. The following local results remain their original evidence snapshot.

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
real OCR or human input. The same complete suite runs in Windows x64 CI; the first hosted
Runner result is recorded above; broader runner reliability remains UNVERIFIED. Clipboard
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
