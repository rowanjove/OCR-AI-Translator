#include "probe_support.hpp"
#include "ocr_adapter.hpp"
#include <memory>

using namespace spike;
namespace {
struct Options {
    std::filesystem::path log;
    std::wstring expected;
    std::wstring readyEvent;
    DWORD baseline = 0, owner = 0, timeout = 5000;
    bool hasBaseline = false;
};
struct Context : Timeline {
    Options opt;
    Log log;
    uint64_t started;
    unsigned events = 0, retries = 0;
    int exitCode = 3;
    bool listener = false, failed = false;
    Context(Options o, uint64_t start) : opt(std::move(o)), log(opt.log), started(start) {}
    double elapsed() const { return static_cast<double>(qpc() - started) * 1000.0 / static_cast<double>(frequency()); }
    bool record(HWND hwnd, const char* phase) {
        auto s = readClipboard(hwnd, opt.expected);
        const auto verdict = classify(s, opt.baseline, opt.owner);
        const auto ms = elapsed();
        observe(s, opt.baseline, opt.owner, opt.hasBaseline, ms, std::string(phase) == "launch_snapshot");
        log.line("{\"event\":" + json(phase) + ",\"qpc\":" + std::to_string(qpc()) + ",\"elapsed_ms\":" + std::to_string(ms) +
                 "," + metadata(s) + ",\"fixture_verdict\":" + json(verdict) + "}");
        return s.status == "clipboard_busy" || s.status == "sequence_changed_during_read";
    }
    void finish() {
        exitCode = failed ? 4 : result();
        log.line("{\"event\":\"summary\",\"launch_matched\":" + std::string(launchMatched ? "true" : "false") +
                 ",\"eventual_matched\":" + (eventualMatched ? "true" : "false") + ",\"late_mismatch\":" + (lateMismatch ? "true" : "false") +
                 ",\"first_match_ms\":" + std::to_string(firstMatchMs) + ",\"elapsed_ms\":" + std::to_string(elapsed()) +
                 ",\"clipboard_events\":" + std::to_string(events) + ",\"lock_retries\":" + std::to_string(retries) +
                 ",\"exit_code\":" + std::to_string(exitCode) + ",\"production_chain_verified\":false}");
    }
};
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<Context*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        c = static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
    }
    if (!c) return DefWindowProcW(hwnd, msg, wp, lp);
    try {
        switch (msg) {
        case WM_CLIPBOARDUPDATE:
            ++c->events;
            if (c->record(hwnd, "clipboard_event") && !SetTimer(hwnd, 2, 20, nullptr)) throw std::runtime_error("retry timer failed");
            return 0;
        case WM_TIMER:
            if (wp == 1) { DestroyWindow(hwnd); return 0; }
            if (wp == 2) {
                ++c->retries;
                if (!c->record(hwnd, "lock_retry")) KillTimer(hwnd, 2);
                return 0;
            }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            if (c->listener) RemoveClipboardFormatListener(hwnd);
            KillTimer(hwnd, 1); KillTimer(hwnd, 2); PostQuitMessage(0); return 0;
        }
    } catch (...) { c->failed = true; DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int selfTest() {
    unsigned n = 0;
    auto check = [&](bool ok) { if (!ok) throw std::runtime_error("self-test failed at " + std::to_string(n + 1)); ++n; };
    Snapshot s; s.status = "text"; s.sequence = 11; s.ownerPid = 42; s.text = L"old";
    check(std::string(classify(s, 10, 42)) == "wrong_or_stale_text");
    s.matched = true;
    check(std::string(classify(s, 11, 42)) == "baseline_not_changed");
    check(std::string(classify(s, 10, 9)) == "owner_mismatch");
    check(std::string(classify(s, 10, 42)) == "fixture_match_only");
    s.status = "sequence_changed_during_read";
    check(std::string(classify(s, 10, 42)) == "unreadable_or_empty");
    s.status = "clipboard_busy";
    check(std::string(classify(s, 10, 42)) == "unreadable_or_empty");
    s.status = "empty_text";
    check(std::string(classify(s, 10, 42)) == "unreadable_or_empty");
    check(compact(L"SP K12\r\n3\t") == L"SPK123");
    check(fixtureTokenPresent(L"Test\n SP K123\r\nMore", L"SPK123"));
    check(!fixtureTokenPresent(L"Probe.exe --expect SPK123 --baseline 1", L"SPK123"));
    check(!fixtureTokenPresent(L"oldSPK123text", L"SPK123"));
    check(tokenValid(L"SPK1234") && !tokenValid(L"a;b") && !tokenValid(L"") && !tokenValid(std::wstring(81, L'A')));
    check(quote(L"C:\\中文 目录\\Probe.exe") == L"\"C:\\中文 目录\\Probe.exe\"");
    check(quote(L"a\\\"b\\") == L"\"a\\\\\\\"b\\\\\"");
    check(json("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    check(hash(L"abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(utf8(L"中文") == "中文");
    check(number(L"5000", 0, 60000) == 5000);
    bool rejected = false; try { number(L"-1", 0, 60000); } catch (...) { rejected = true; }
    check(rejected);
    Timeline timeline;
    s.status = "text"; s.matched = false;
    timeline.observe(s, 10, 42, true, 0, true);
    check(timeline.result() == 3);
    s.matched = true;
    timeline.observe(s, 10, 42, true, 120, false);
    check(timeline.result() == 2 && !timeline.launchMatched && timeline.firstMatchMs == 120);
    s.status = "clipboard_busy";
    timeline.observe(s, 10, 42, true, 130, false);
    check(timeline.result() == 2);
    s.status = "text"; s.matched = false;
    timeline.observe(s, 10, 42, true, 140, false);
    check(timeline.result() == 3 && timeline.lateMismatch);
    timeline = {}; s.matched = true;
    timeline.observe(s, 10, 42, false, 0, true);
    check(timeline.result() == 3); // No pre-capture baseline: cannot certify even a known fixture.
    timeline = {};
    timeline.observe(s, 10, 42, true, 0, true);
    check(timeline.result() == 0);
    timeline.observe(s, 11, 42, true, 2, false);
    check(timeline.result() == 3); // Reverted baseline is not accepted.
    rejected = false; try { number(L"60001", 0, 60000); } catch (...) { rejected = true; }
    check(rejected);
    std::cout << "PASS " << n << " checks (pure tests; no Snipaste, no clipboard write)\n";
    return 0;
}
int diagnoseIntegration(const std::filesystem::path& logPath) {
    bool isElevated = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isElevated = elevation.TokenIsElevated != 0;
        }
        CloseHandle(token);
    }

    auto snipastePath = sat::SnipasteReverseAdapter::detectSnipasteExecutable();
    bool pathFound = !snipastePath.empty();

    HWND snipasteWnd = FindWindowW(L"Snipaste", nullptr);
    if (!snipasteWnd) snipasteWnd = FindWindowW(nullptr, L"Snipaste");
    DWORD snipastePid = 0;
    bool uipiBlocked = false;
    if (snipasteWnd) {
        GetWindowThreadProcessId(snipasteWnd, &snipastePid);
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(snipasteWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &result)) {
            if (GetLastError() == 5 /* ERROR_ACCESS_DENIED */) {
                uipiBlocked = true;
            }
        }
    }

    bool launchAttempted = false;
    bool launchTimedOut = false;
    DWORD exitCodeBeforeKill = 0;
    DWORD launchExitCode = 0;
    if (pathFound) {
        launchAttempted = true;
        std::wstring cmd = L"\"" + snipastePath.wstring() + L"\" --help";
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 1500);
            if (wait == WAIT_TIMEOUT) {
                launchTimedOut = true;
                GetExitCodeProcess(pi.hProcess, &exitCodeBeforeKill); // 259 == STILL_ACTIVE
                TerminateProcess(pi.hProcess, 9);
                WaitForSingleObject(pi.hProcess, 200);
                GetExitCodeProcess(pi.hProcess, &launchExitCode);
            } else {
                GetExitCodeProcess(pi.hProcess, &launchExitCode);
                exitCodeBeforeKill = launchExitCode;
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    DWORD baselineSeq = GetClipboardSequenceNumber();
    bool unicodeAvailable = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;

    std::string report = "{\"event\":\"integration_diagnostic\""
        ",\"current_process_elevated\":" + std::string(isElevated ? "true" : "false") +
        ",\"snipaste_path\":" + json(utf8(snipastePath.wstring())) +
        ",\"snipaste_path_found\":" + (pathFound ? "true" : "false") +
        ",\"snipaste_window_detected\":" + (snipasteWnd ? "true" : "false") +
        ",\"snipaste_pid\":" + std::to_string(snipastePid) +
        ",\"uipi_blocked\":" + (uipiBlocked ? "true" : "false") +
        ",\"launch_attempted\":" + (launchAttempted ? "true" : "false") +
        ",\"launch_timed_out\":" + (launchTimedOut ? "true" : "false") +
        ",\"exit_code_before_kill\":" + std::to_string(exitCodeBeforeKill) +
        ",\"launch_exit_code\":" + std::to_string(launchExitCode) +
        ",\"launch_status_note\":\"259 represents Windows STILL_ACTIVE indicator during the 1500ms observation window, not evidence of permanent deadlock on its own\"" +
        ",\"baseline_sequence\":" + std::to_string(baselineSeq) +
        ",\"clipboard_unicode_available\":" + (unicodeAvailable ? "true" : "false") +
        ",\"recommended_architecture\":\"reverse_integration\"" +
        ",\"production_chain_verified\":false}";

    std::cout << report << "\n";
    if (!logPath.empty()) {
        Log log(logPath);
        log.line(report);
    }
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    const auto started = qpc();
    const DWORD entrySequence = GetClipboardSequenceNumber();
    try {
        if (argc >= 2 && std::wstring(argv[1]) == L"--diagnose-integration") {
            std::filesystem::path logPath;
            if (argc == 4 && std::wstring(argv[2]) == L"--log") logPath = argv[3];
            return diagnoseIntegration(logPath);
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") return selfTest();
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test-deadline") {
            ProcessDeadline deadline(50);
            Handle neverSignaled(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!neverSignaled.value) throw std::runtime_error("deadline test event creation failed");
            std::cout << "Deadline test armed; expected self-termination with exit 4\n" << std::flush;
            WaitForSingleObject(neverSignaled.value, 1000);
            return 5; // Watchdog did not work; must not be mistaken for success.
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--help") {
            std::cout << "Probe --log absolute.jsonl [--expect TOKEN --baseline SEQ] [--owner-pid PID] [--timeout-ms 0..60000]\n"
                         "      --diagnose-integration [--log absolute.jsonl]\n"
                         "Exit: 0 launch fixture match, 2 late fixture match (race), 3 unconfirmed, 4 diagnostic failure.\n"
                         "Optional --ready-event Local\\SAT.P0.Ready.TOKEN is a native test handshake, not OCR readiness.\n"
                         "All fixture outcomes leave production provenance UNVERIFIED. Text is never logged.\n";
            return 0;
        }
        Options o; std::vector<std::wstring> seen;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (std::find(seen.begin(), seen.end(), arg) != seen.end()) throw std::runtime_error("duplicate option");
            seen.push_back(arg);
            if (i + 1 >= argc) throw std::runtime_error("missing option value");
            const std::wstring value = argv[++i];
            if (arg == L"--log") o.log = value;
            else if (arg == L"--expect") { if (!tokenValid(value)) throw std::runtime_error("expect must be an alphanumeric fixture token"); o.expected = value; }
            else if (arg == L"--baseline") { o.baseline = number(value, 0, MAXDWORD); o.hasBaseline = true; }
            else if (arg == L"--owner-pid") o.owner = number(value, 1, MAXDWORD);
            else if (arg == L"--timeout-ms") o.timeout = number(value, 0, 60000);
            else if (arg == L"--ready-event") {
                const std::wstring prefix = L"Local\\SAT.P0.Ready.";
                if (value.rfind(prefix, 0) != 0 || !tokenValid(value.substr(prefix.size()))) throw std::runtime_error("invalid test ready-event name");
                o.readyEvent = value;
            }
            else throw std::runtime_error("unknown option");
        }
        if (o.log.empty() || !o.log.is_absolute()) throw std::runtime_error("--log requires a new absolute file path");
        if (!o.expected.empty() && !o.hasBaseline) throw std::runtime_error("fixture verification requires pre-capture --baseline");
        ProcessDeadline deadline(o.timeout + 1500); // Fail closed even if a clipboard owner blocks synchronous rendering.
        if (GetFileAttributesW(o.log.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const auto duplicate = std::filesystem::path(o.log.wstring() + L".duplicate-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
            Log rejection(duplicate);
            rejection.line("{\"event\":\"duplicate_invocation_rejected\",\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"production_chain_verified\":false}");
            throw std::runtime_error("log exists; duplicate invocation recorded separately without overwriting evidence");
        }
        Context c(std::move(o), started);
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) throw std::runtime_error("GetProcessTimes failed");
        c.log.line("{\"event\":\"process_entry\",\"schema_version\":1,\"pid\":" + std::to_string(GetCurrentProcessId()) +
                   ",\"entry_sequence\":" + std::to_string(entrySequence) + ",\"utc_100ns\":" + std::to_string(utc100ns()) +
                   ",\"process_creation_100ns\":" + std::to_string((static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime) +
                   ",\"qpc\":" + std::to_string(started) + ",\"qpc_frequency\":" + std::to_string(frequency()) + "}");
        const bool busy = c.record(nullptr, "launch_snapshot");
        if (c.opt.timeout) {
            WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SAT.P0.Probe"; wc.lpfnWndProc = proc;
            if (!RegisterClassW(&wc)) throw std::runtime_error("RegisterClass failed");
            // A hidden top-level window receives clipboard broadcasts; no tray, hooks or visible UI.
            HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"P0 Probe", 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, &c);
            if (!hwnd) throw std::runtime_error("CreateWindow failed");
            struct Window { HWND h; ~Window() { if (IsWindow(h)) DestroyWindow(h); } } window{hwnd};
            if (!AddClipboardFormatListener(hwnd)) throw std::runtime_error("clipboard listener registration failed");
            c.listener = true;
            if (!SetTimer(hwnd, 1, c.opt.timeout, nullptr)) throw std::runtime_error("deadline timer failed");
            // Recheck AFTER listener registration, covering changes between initial read and registration.
            const bool retry = c.record(hwnd, "listener_ready_snapshot");
            if ((busy || retry) && !SetTimer(hwnd, 2, 20, nullptr)) throw std::runtime_error("retry timer failed");
            c.log.line("{\"event\":\"listener_ready\",\"elapsed_ms\":" + std::to_string(c.elapsed()) + "}");
            if (!c.opt.readyEvent.empty()) {
                Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, c.opt.readyEvent.c_str()));
                if (!ready.value || !SetEvent(ready.value)) throw std::runtime_error("test ready-event signaling failed");
            }
            MSG msg{}; BOOL status;
            while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if (status == -1) c.failed = true;
        }
        c.finish();
        std::cout << "Probe complete: exit=" << c.exitCode << "; production_chain_verified=false\n";
        return c.exitCode;
    } catch (const std::exception& e) { std::cerr << "Probe diagnostic error: " << e.what() << '\n'; return 4; }
}
