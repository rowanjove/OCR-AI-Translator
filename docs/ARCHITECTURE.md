# Architecture

The current implementation is C++20 / Win32 x64 with static MSVC CRT.
Translator owns native RichEdit text controls, GDI text and Direct2D UI icons.
There is no embedded browser, model, database or background service.

| Module | Responsibility |
| --- | --- |
| `src/translator.cpp` | Windows, CLI, mutex/WM_COPYDATA single-instance delivery, input/request revisions, drafts, profile UI and in-memory version history |
| `src/translation_client.*` | Asynchronous WinHTTP, endpoint validation, deadlines, cancellation and secret cleanup |
| `src/sse_parser.*`, `src/json.hpp` | Bounded protocol parsing and Unicode validation; in-tree code |
| `src/config_store.*` | Schema validation, atomic JSON save, current-user DPAPI secret files and profile role bindings |
| `src/ocr_adapter.*` | IOcrAdapter interface and explicit-path Snipaste reverse capture adapter |
| `src/ui_*.hpp`, `src/translator_ui.hpp` | Native layout, frame, typography, drawing and keyboard actions |
| `src/probe*`, `src/spike_runner.cpp` | Development-only OCR investigation tools, absent from portable ZIP |
| `src/app_version.h.in`, `src/app_resources.rc` | CMake version source and embedded PE version/icon |
| `tests/`, guarded test headers | Isolated local protocol, DPAPI, HWND and process fixtures |

Input enters a versioned session. Requests carry request ID, revision and
source session ID. Only current events may update the UI. Successful complete
results enter memory history; failed or cancelled operations restore a matching
completed version. Editing hides the translation pane and requires explicit
submission. Closing the final window cancels requests and exits this process.

Snipaste is the only implemented OCR adapter. Clipboard sequence changes do
not authenticate OCR provenance. Candidate confirmation and session/deadline
checks reduce mistakes; real OCR remains unverified. Other OCR adapters and
screenshot-file OCR are future work.

Configuration lives under `%LOCALAPPDATA%/SnipasteAITranslator`. Test-only
SAT_CONFIG_DIR / SAT_TEST_MODE hooks are compiled into TranslatorTest and
CoreTests, not Translator. DPAPI ciphertexts are private data, never suitable
for source publication. See [testing](TESTING.md) for isolation and current
release failures.
