#include "probe_support.hpp"
#include <dwmapi.h>
#include <memory>

using namespace spike;
namespace {
struct Options {
    std::filesystem::path snipaste, out, probe;
    std::wstring engine = L"UNCONFIRMED", language = L"en";
    DWORD count = 1, timeout = 5000, trialDeadline = 45000;
    bool automatic = false, cancel = false, execOnly = false, hotkey = false;
};
struct Runner {
    Options opt;
    Log log;
    std::wstring token, display, status;
    std::filesystem::path probeLog;
    std::unique_ptr<Handle> caller;
    HWND hwnd = nullptr, button = nullptr;
    HFONT font = nullptr;
    unsigned trial = 0, completed = 0, passes = 0, late = 0, failures = 0, noExec = 0, externalExecs = 0;
    DWORD baseline = 0;
    uint64_t trialStart = 0;
    bool running = false, triggerAfterPaint = false;
    int exitCode = 3;
    explicit Runner(Options o) : opt(std::move(o)), log(opt.out / L"runner.jsonl") {}
    ~Runner() { cleanupCaller(); if (font) DeleteObject(font); }
    std::wstring childCommand() const {
        return quote(opt.probe.wstring()) + L" --log " + quote(probeLog.wstring()) + L" --expect " + token +
            L" --baseline " + std::to_wstring(baseline) + L" --timeout-ms " + std::to_wstring(opt.timeout);
    }
    void cleanupCaller() noexcept {
        if (!caller) return;
        if (WaitForSingleObject(caller->value, 0) == WAIT_TIMEOUT) {
            const DWORD pid = GetProcessId(caller->value);
            // This is ONLY the process created by this trial; never the pre-existing Snipaste instance.
            const bool stopped = TerminateProcess(caller->value, 5) != FALSE;
            const DWORD wait = stopped ? WaitForSingleObject(caller->value, 500) : WAIT_FAILED;
            try { log.line("{\"event\":\"trial_caller_cleanup\",\"pid\":" + std::to_string(pid) +
                           ",\"termination_requested\":" + (stopped ? "true" : "false") + ",\"wait_result\":" + std::to_string(wait) + "}"); } catch (...) {}
        }
        caller.reset();
    }
    void next() {
        if (++trial > opt.count) { finish(); return; }
        token = randomToken();
        if (opt.language == L"en") display = L"Native screenshot OCR test\n" + token + L"\nTranslate only this fixture.";
        else if (opt.language == L"zh") display = L"原生截图识别测试\n" + token + L"\n只识别当前测试文字。";
        else if (opt.language == L"ja") display = L"スクリーンショット認識テスト\n" + token + L"\nこのテスト文字を読み取ります。";
        else display = L"中文 English 混合文字测试\n" + token + L"\n数字 2026 与标点。";
        probeLog = opt.out / (L"trial-" + std::to_wstring(trial) + L".jsonl");
        if (opt.hotkey) {
            baseline = GetClipboardSequenceNumber();
            Log command(opt.out / (L"trial-" + std::to_wstring(trial) + L"-command.txt"));
            command.line(utf8(L"snip -o ocr_clipboard;exec(" + childCommand() + L")"));
            SetWindowTextW(button, L"命令已绑定：开始等待热键");
        }
        status = L"测试 " + std::to_wstring(trial) + L" / " + std::to_wstring(opt.count) +
            (opt.automatic ? L"：即将仅截取白色测试区" : L"：点击按钮，框选白色测试区；取消用例请按 Esc");
        running = false;
        EnableWindow(button, TRUE);
        triggerAfterPaint = opt.automatic;
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    void capture() {
        if (running) return;
        running = true; triggerAfterPaint = false;
        EnableWindow(button, FALSE);
        if (!opt.hotkey) baseline = GetClipboardSequenceNumber();
        const std::wstring child = childCommand();
        // Parentheses/semicolon/control characters in paths are rejected at the Snipaste grammar boundary.
        const std::wstring output = L"ocr_clipboard;exec(" + child + L")";
        std::wstring args = L"snip ";
        RECT captureRect{24, 24, 736, 238};
        const UINT dpi = GetDpiForWindow(hwnd);
        captureRect.left = MulDiv(captureRect.left, static_cast<int>(dpi), 96);
        captureRect.top = MulDiv(captureRect.top, static_cast<int>(dpi), 96);
        captureRect.right = MulDiv(captureRect.right, static_cast<int>(dpi), 96);
        captureRect.bottom = MulDiv(captureRect.bottom, static_cast<int>(dpi), 96);
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&captureRect), 2);
        if (opt.automatic) args += L"--area " + std::to_wstring(captureRect.left) + L" " + std::to_wstring(captureRect.top) + L" " +
            std::to_wstring(captureRect.right - captureRect.left) + L" " + std::to_wstring(captureRect.bottom - captureRect.top) + L" ";
        args += L"-o " + quote(output); // no shell, no --block as OCR completion signal.
        if (opt.execOnly) args = L"external " + child;
        trialStart = GetTickCount64();
        log.line("{\"event\":\"capture_requested\",\"trial\":" + std::to_string(trial) + ",\"baseline\":" + std::to_string(baseline) +
                 ",\"utc_100ns\":" + std::to_string(utc100ns()) + ",\"qpc\":" + std::to_string(qpc()) +
                 ",\"fixture_sha256\":" + json(hash(token)) + ",\"snipaste_command\":" + json(utf8(args)) + "}");
        if (!opt.hotkey) {
            caller = std::make_unique<Handle>(launch(opt.snipaste, args));
            log.line("{\"event\":\"caller_created\",\"pid\":" + std::to_string(GetProcessId(caller->value)) + "}");
        } else log.line("{\"event\":\"hotkey_armed\",\"arm_sequence\":" + std::to_string(GetClipboardSequenceNumber()) + "}");
        status = L"等待 Snipaste / Probe 实际事件；不会把 --block 或经过时间视为 OCR 完成";
        InvalidateRect(hwnd, nullptr, TRUE);
        if (!SetTimer(hwnd, 1, 100, nullptr)) throw std::runtime_error("runner deadline timer failed");
    }
    void check() {
        if (!running) return;
        std::ifstream in(probeLog, std::ios::binary);
        std::string line, summary; DWORD probePid = 0;
        if (in && std::filesystem::file_size(probeLog) > 1024 * 1024) throw std::runtime_error("oversized probe log");
        while (std::getline(in, line)) {
            if (line.find("\"event\":\"summary\"") != std::string::npos) summary = line;
            if (line.find("\"event\":\"process_entry\"") != std::string::npos) {
                const auto at = line.find("\"pid\":");
                if (at != std::string::npos) probePid = static_cast<DWORD>(std::stoul(line.substr(at + 6)));
            }
        }
        if (!summary.empty()) {
            if (probePid) {
                Handle probeProcess(OpenProcess(SYNCHRONIZE, FALSE, probePid));
                if (probeProcess.value && WaitForSingleObject(probeProcess.value, 0) == WAIT_TIMEOUT) {
                    if (GetTickCount64() - trialStart >= opt.trialDeadline) throw std::runtime_error("probe did not exit after summary");
                    return;
                }
                if (!probeProcess.value && GetLastError() != ERROR_INVALID_PARAMETER) throw std::runtime_error("cannot verify probe exit");
            }
            const bool pass = summary.find("\"exit_code\":0") != std::string::npos;
            const bool race = summary.find("\"exit_code\":2") != std::string::npos;
            bool duplicate = false;
            for (const auto& entry : std::filesystem::directory_iterator(opt.out)) {
                if (entry.path().filename().wstring().rfind(probeLog.filename().wstring() + L".duplicate-", 0) == 0) duplicate = true;
            }
            if (duplicate) ++failures;
            else if (opt.execOnly) ++externalExecs;
            else if (pass && !opt.cancel) ++passes;
            else if (race && !opt.cancel) ++late;
            else ++failures;
            log.line("{\"event\":\"trial_result\",\"trial\":" + std::to_string(trial) + ",\"result\":" +
                     json(duplicate ? "duplicate_probe_launch" : (opt.execOnly ? "external_probe_started_no_ocr_test" : (opt.cancel ? "unexpected_exec_on_cancel_test" : (pass ? "launch_fixture_match" : (race ? "exec_before_ocr" : "unconfirmed"))))) + "}");
            endTrial();
        } else if (GetTickCount64() - trialStart >= opt.trialDeadline) {
            if (opt.cancel && !std::filesystem::exists(probeLog)) ++noExec;
            else ++failures;
            DWORD code = STILL_ACTIVE;
            if (caller) GetExitCodeProcess(caller->value, &code);
            log.line("{\"event\":\"trial_result\",\"trial\":" + std::to_string(trial) + ",\"result\":" +
                     json(opt.cancel && !std::filesystem::exists(probeLog) ? "no_exec_observed_cancel_not_independently_confirmed" : "no_complete_probe_before_deadline") +
                     ",\"snipaste_caller_exit\":" + std::to_string(code) + "}");
            endTrial();
        }
    }
    void endTrial() {
        KillTimer(hwnd, 1); running = false;
        ++completed;
        cleanupCaller();
        if (opt.automatic) { if (failures || late) finish(); else next(); }
        else {
            status = L"本轮已记录。点击继续；关闭窗口结束。结果仅证明测试文本，不代表生产链路通过。";
            SetWindowTextW(button, L"下一个测试"); EnableWindow(button, TRUE); InvalidateRect(hwnd, nullptr, TRUE);
            if (trial == opt.count) finish();
        }
    }
    void finish() {
        log.line("{\"event\":\"batch_summary\",\"requested_trials\":" + std::to_string(opt.count) + ",\"completed_trials\":" + std::to_string(completed) +
                 ",\"launch_fixture_matches\":" + std::to_string(passes) +
                 ",\"late_matches\":" + std::to_string(late) + ",\"unconfirmed_or_failed\":" + std::to_string(failures) +
                 ",\"no_exec_observed\":" + std::to_string(noExec) + ",\"production_chain_verified\":false}");
        log.line("{\"event\":\"external_exec_summary\",\"external_probe_starts\":" + std::to_string(externalExecs) + "}");
        exitCode = (passes == opt.count && !opt.cancel) || (opt.execOnly && externalExecs == opt.count) ? 0 : 3;
        DestroyWindow(hwnd);
    }
};
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* r = reinterpret_cast<Runner*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        r = static_cast<Runner*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(r)); r->hwnd = hwnd;
    }
    if (!r) return DefWindowProcW(hwnd, msg, wp, lp);
    try {
        switch (msg) {
        case WM_CREATE: {
            r->button = CreateWindowExW(0, L"BUTTON", L"开始截图测试", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        24, 280, 250, 40, hwnd, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
            if (!r->button) throw std::runtime_error("button creation failed");
            const UINT dpi = GetDpiForWindow(hwnd);
            r->font = CreateFontW(-MulDiv(28, static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            if (!r->font) throw std::runtime_error("font creation failed");
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT bounds{}; GetClientRect(hwnd, &bounds); FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
            const UINT dpi = GetDpiForWindow(hwnd);
            RECT textRect{MulDiv(30, static_cast<int>(dpi), 96), MulDiv(35, static_cast<int>(dpi), 96),
                          MulDiv(720, static_cast<int>(dpi), 96), MulDiv(230, static_cast<int>(dpi), 96)};
            auto old = SelectObject(dc, r->font); SetTextColor(dc, RGB(15, 23, 42)); SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, r->display.c_str(), -1, &textRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            RECT statusRect{24, bounds.bottom - 90, bounds.right - 24, bounds.bottom - 20};
            DrawTextW(dc, r->status.c_str(), -1, &statusRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            EndPaint(hwnd, &ps);
            if (r->triggerAfterPaint) { r->triggerAfterPaint = false; PostMessageW(hwnd, WM_APP + 1, 0, 0); }
            return 0;
        }
        case WM_APP + 1:
            // Wait for compositor completion, not an arbitrary delay. This is fixture readiness, NOT OCR synchronization.
            DwmFlush(); r->capture(); return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == 1 && HIWORD(wp) == BN_CLICKED) {
                if (r->trialStart != 0 && !r->running) { r->trialStart = 0; r->next(); }
                else r->capture();
                return 0;
            }
            break;
        case WM_TIMER: r->check(); return 0;
        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (r->font) DeleteObject(r->font);
            r->font = CreateFontW(-MulDiv(28, static_cast<int>(HIWORD(wp)), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            if (!r->font) throw std::runtime_error("DPI font recreation failed");
            const int dpi = static_cast<int>(HIWORD(wp));
            MoveWindow(r->button, MulDiv(24, dpi, 96), MulDiv(280, dpi, 96), MulDiv(250, dpi, 96), MulDiv(40, dpi, 96), TRUE);
            InvalidateRect(hwnd, nullptr, TRUE); return 0;
        }
        case WM_CLOSE:
            r->log.line("{\"event\":\"operator_closed\",\"pending_trial\":" + std::string(r->running ? "true" : "false") + "}");
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) {
        r->exitCode = 4;
        try { r->log.line("{\"event\":\"diagnostic_failure\",\"error\":" + json(e.what()) + "}"); } catch (...) {}
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
bool grammarSafe(const std::wstring& s) {
    return s.find_first_of(L";()\"\r\n") == std::wstring::npos;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--help") {
            std::cout << "SpikeRunner --snipaste absolute.exe --out new-absolute-directory [--auto --count 1..100]\n"
                         " [--probe absolute.exe] [--engine label] [--language en|zh|mixed|ja]\n"
                         " [--timeout-ms 500..60000] [--trial-deadline-ms 1000..120000] [--cancel] [--exec-only] [--hotkey]\n"
                         "Manual default. --auto captures only the synthetic fixture area. OCR changes your clipboard.\n"
                         "No tests imply Snipaste PRO/license/version; record those from built-in Help.\n";
            return 0;
        }
        Options o; o.probe = executable().parent_path() / L"Probe.exe";
        std::vector<std::wstring> seen;
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (std::find(seen.begin(), seen.end(), a) != seen.end()) throw std::runtime_error("duplicate option");
            seen.push_back(a);
            if (a == L"--auto") { o.automatic = true; continue; }
            if (a == L"--cancel") { o.cancel = true; continue; }
            if (a == L"--exec-only") { o.execOnly = true; continue; }
            if (a == L"--hotkey") { o.hotkey = true; continue; }
            if (i + 1 >= argc) throw std::runtime_error("missing option value");
            const std::wstring v = argv[++i];
            if (a == L"--snipaste") o.snipaste = v;
            else if (a == L"--probe") o.probe = v;
            else if (a == L"--out") o.out = v;
            else if (a == L"--count") o.count = number(v, 1, 100);
            else if (a == L"--timeout-ms") o.timeout = number(v, 500, 60000);
            else if (a == L"--trial-deadline-ms") o.trialDeadline = number(v, 1000, 120000);
            else if (a == L"--engine") { if (v.empty() || v.size() > 80) throw std::runtime_error("invalid engine label"); o.engine = v; }
            else if (a == L"--language") o.language = v;
            else throw std::runtime_error("unknown option");
        }
        if (o.cancel && o.automatic) throw std::runtime_error("cancel test requires manual selection and real Esc");
        if (o.cancel && o.execOnly) throw std::runtime_error("cancel cannot be combined with exec-only");
        if (o.hotkey && (o.automatic || o.execOnly)) throw std::runtime_error("hotkey requires manual mode and OCR test");
        if (o.trialDeadline <= o.timeout + 2000) throw std::runtime_error("trial deadline must exceed observation timeout by more than 2000ms");
        if (o.language != L"en" && o.language != L"zh" && o.language != L"mixed" && o.language != L"ja") throw std::runtime_error("invalid fixture language");
        for (const auto& p : {o.snipaste, o.probe}) {
            const DWORD attrs = GetFileAttributesW(p.c_str()); // std::filesystem::exists follows Store alias reparse points and can fail.
            if (!p.is_absolute() || attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                p.extension() != L".exe" || !grammarSafe(p.wstring()))
                throw std::runtime_error("executable must exist at an absolute .exe path with no Snipaste grammar delimiters");
        }
        if (!o.out.is_absolute() || !grammarSafe(o.out.wstring())) throw std::runtime_error("output must be a new absolute grammar-safe directory");
        if (!std::filesystem::create_directories(o.out)) throw std::runtime_error("output directory already exists; use a new run directory");
        Runner r(std::move(o));
        r.log.line("{\"event\":\"runner_start\",\"schema_version\":1,\"utc_100ns\":" + std::to_string(utc100ns()) +
                   ",\"engine_operator_label\":" + json(utf8(r.opt.engine)) + ",\"fixture_language\":" + json(utf8(r.opt.language)) +
                   ",\"auto_area\":" + (r.opt.automatic ? "true" : "false") + ",\"cancel_test\":" + (r.opt.cancel ? "true" : "false") +
                   ",\"external_only\":" + (r.opt.execOnly ? "true" : "false") +
                   ",\"hotkey_mode\":" + (r.opt.hotkey ? "true" : "false") +
                   ",\"snipaste_executable\":" + json(utf8(r.opt.snipaste.wstring())) + ",\"license_verified\":false}");
        WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = proc;
        wc.lpszClassName = L"SAT.P0.Fixture"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        if (!RegisterClassW(&wc)) throw std::runtime_error("fixture class registration failed");
        POINT cursor{}; GetCursorPos(&cursor);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi)) throw std::runtime_error("monitor query failed");
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"P0-Spike — 合成 OCR 测试区（不是翻译成品）",
                                   WS_OVERLAPPEDWINDOW, mi.rcWork.left + 30, mi.rcWork.top + 30, 800, 440,
                                   nullptr, nullptr, wc.hInstance, &r);
        if (!hwnd) throw std::runtime_error("fixture window creation failed");
        struct Window { HWND h; ~Window() { if (IsWindow(h)) DestroyWindow(h); } } window{hwnd};
        const UINT dpi = GetDpiForWindow(hwnd);
        RECT outer{0, 0, MulDiv(760, static_cast<int>(dpi), 96), MulDiv(400, static_cast<int>(dpi), 96)};
        AdjustWindowRectExForDpi(&outer, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST, dpi);
        const int width = std::min(outer.right - outer.left, mi.rcWork.right - mi.rcWork.left);
        const int height = std::min(outer.bottom - outer.top, mi.rcWork.bottom - mi.rcWork.top);
        SetWindowPos(hwnd, nullptr, mi.rcWork.left, mi.rcWork.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        MoveWindow(r.button, MulDiv(24, static_cast<int>(dpi), 96), MulDiv(280, static_cast<int>(dpi), 96),
                   MulDiv(250, static_cast<int>(dpi), 96), MulDiv(40, static_cast<int>(dpi), 96), FALSE);
        r.next(); ShowWindow(hwnd, SW_SHOWNORMAL); UpdateWindow(hwnd);
        MSG msg{}; BOOL status;
        while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (status == -1) return 4;
        std::cout << "SpikeRunner exit=" << r.exitCode << "; production_chain_verified=false\n";
        return r.exitCode;
    } catch (const std::exception& e) { std::cerr << "SpikeRunner: " << e.what() << '\n'; return 4; }
}
