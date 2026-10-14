#include "probe_support.hpp"
#include <memory>

using namespace spike;
namespace {
class EnvironmentUnavailable : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class IsolatedClipboard {
    HWINSTA originalStation = GetProcessWindowStation(), station = nullptr;
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
public:
    std::wstring desktopName;
    IsolatedClipboard() {
        const auto name = L"SATP0" + randomToken();
        station = CreateWindowStationW(name.c_str(), 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station) {
            const DWORD error = GetLastError();
            const auto message = "cannot create private window station (Win32 " + std::to_string(error) + "); refusing to touch user clipboard";
            if (error == ERROR_ACCESS_DENIED) throw EnvironmentUnavailable(message);
            throw std::runtime_error(message);
        }
        if (!SetProcessWindowStation(station)) { CloseWindowStation(station); throw std::runtime_error("cannot select private window station"); }
        desktop = CreateDesktopW(L"test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop || !SetThreadDesktop(desktop)) {
            SetProcessWindowStation(originalStation);
            if (desktop) CloseDesktop(desktop);
            CloseWindowStation(station);
            throw std::runtime_error("cannot select private desktop; refusing to touch user clipboard");
        }
        desktopName = name + L"\\test";
    }
    ~IsolatedClipboard() {
        SetThreadDesktop(originalDesktop); SetProcessWindowStation(originalStation);
        CloseDesktop(desktop); CloseWindowStation(station);
    }
};
void writeText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) throw std::runtime_error("isolated clipboard open failed");
    struct Close { ~Close() { CloseClipboard(); } } close;
    if (!EmptyClipboard()) throw std::runtime_error("isolated clipboard empty failed");
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!h) throw std::runtime_error("allocation failed");
    void* p = GlobalLock(h);
    if (!p) { GlobalFree(h); throw std::runtime_error("memory lock failed"); }
    memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    if (!SetClipboardData(CF_UNICODETEXT, h)) { GlobalFree(h); throw std::runtime_error("isolated clipboard write failed"); }
}
HANDLE startProbe(const std::wstring& desktop, const std::wstring& args) {
    const auto app = executable().parent_path() / L"Probe.exe";
    auto cmd = quote(app.wstring()) + L" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si); si.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        throw std::runtime_error("isolated probe launch failed");
    CloseHandle(pi.hThread); return pi.hProcess;
}
struct Child {
    Handle handle;
    explicit Child(HANDLE h) : handle(h) {}
    ~Child() { if (WaitForSingleObject(handle.value, 0) == WAIT_TIMEOUT) { TerminateProcess(handle.value, 4); WaitForSingleObject(handle.value, 1000); } }
    DWORD exit() {
        if (WaitForSingleObject(handle.value, 5000) != WAIT_OBJECT_0) throw std::runtime_error("native probe deadline exceeded");
        DWORD code{}; if (!GetExitCodeProcess(handle.value, &code)) throw std::runtime_error("exit code unavailable"); return code;
    }
};
std::string load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}
int wmain() {
    try {
        // Never switch the interactive desktop. All writes happen in a separate, non-visible window station.
        IsolatedClipboard isolated;
        HWND owner = CreateWindowExW(0, L"STATIC", L"isolated test owner", 0, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!owner) throw std::runtime_error("isolated owner window creation failed");
        struct Window { HWND h; ~Window() { DestroyWindow(h); } } window{owner};
        const auto directory = executable().parent_path().parent_path() / L"native-test-evidence" / randomToken();
        std::filesystem::create_directories(directory);
        unsigned checks = 0;
        auto check = [&](bool ok) { if (!ok) throw std::runtime_error("native check failed at " + std::to_string(checks + 1)); ++checks; };
        const std::wstring token = L"SPK987654321123456789";
        const std::wstring fixture = L"合成 Native fixture\n" + token + L"\nOnly test data";
        const auto run = [&](const wchar_t* name, DWORD baseline, DWORD timeout, const std::wstring& event = L"") {
            return L"--log " + quote((directory / name).wstring()) + L" --expect " + token + L" --baseline " + std::to_wstring(baseline) +
                L" --owner-pid " + std::to_wstring(GetCurrentProcessId()) + L" --timeout-ms " + std::to_wstring(timeout) +
                (event.empty() ? L"" : L" --ready-event " + quote(event));
        };
        writeText(owner, L"OLD unrelated private clipboard");
        const DWORD baseline = GetClipboardSequenceNumber();
        writeText(owner, fixture);
        {
            Child child(startProbe(isolated.desktopName, run(L"immediate.jsonl", baseline, 150)));
            check(child.exit() == 0);
            const auto log = load(directory / L"immediate.jsonl");
            check(log.find("\"launch_matched\":true") != std::string::npos);
            check(log.find(utf8(fixture)) == std::string::npos && log.find("Only test data") == std::string::npos);
        }
        writeText(owner, L"OLD unrelated private clipboard");
        const DWORD lateBaseline = GetClipboardSequenceNumber();
        const auto eventName = L"Local\\SAT.P0.Ready." + randomToken();
        Handle ready(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
        if (!ready.value) throw std::runtime_error("ready event creation failed");
        {
            Child child(startProbe(isolated.desktopName, run(L"late.jsonl", lateBaseline, 600, eventName)));
            if (WaitForSingleObject(ready.value, 3000) != WAIT_OBJECT_0) throw std::runtime_error("listener readiness handshake failed");
            writeText(owner, fixture); // Actual clipboard event AFTER Probe is listening, without a fixed sleep.
            check(child.exit() == 2);
            const auto log = load(directory / L"late.jsonl");
            check(log.find("\"launch_matched\":false") != std::string::npos && log.find("\"eventual_matched\":true") != std::string::npos);
            check(log.find("\"event\":\"clipboard_event\"") != std::string::npos);
        }
        {
            const DWORD same = GetClipboardSequenceNumber();
            Child child(startProbe(isolated.desktopName, run(L"unchanged.jsonl", same, 100)));
            check(child.exit() == 3); // Correct token but no change from baseline must fail.
        }
        writeText(owner, L"Probe.exe --expect " + token + L" --baseline 1");
        {
            Child child(startProbe(isolated.desktopName, run(L"command-not-ocr.jsonl", baseline, 100)));
            check(child.exit() == 3); // Copying a command containing the token is not valid OCR.
        }
        writeText(owner, L"");
        {
            Child child(startProbe(isolated.desktopName, run(L"empty.jsonl", baseline, 100)));
            check(child.exit() == 3);
        }
        {
            Child child(startProbe(isolated.desktopName, run(L"immediate.jsonl", baseline, 100)));
            check(child.exit() == 4); // Never overwrite existing evidence.
        }
        std::cout << "PASS " << checks << " native isolated checks; user's interactive clipboard untouched\n";
        std::cout << "Evidence: " << utf8(directory.wstring()) << '\n';
        return 0;
    } catch (const EnvironmentUnavailable& e) { std::cerr << "ENVIRONMENT SKIP (not passed): " << e.what() << '\n'; return 77; }
    catch (const std::exception& e) { std::cerr << "Native clipboard tests: " << e.what() << '\n'; return 1; }
}
