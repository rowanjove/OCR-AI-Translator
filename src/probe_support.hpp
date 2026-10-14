#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace spike {
constexpr size_t maxClipboardBytes = 1024 * 1024;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
class ProcessDeadline {
    Handle stop;
    std::thread watchdog;
public:
    explicit ProcessDeadline(DWORD ms) : stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!stop.value) throw std::runtime_error("deadline event creation failed");
        watchdog = std::thread([h = stop.value, ms] {
            const DWORD result = WaitForSingleObject(h, ms);
            if (result != WAIT_OBJECT_0) {
                // GetClipboardData can ask a foreign owner to render data synchronously.
                // An independent watchdog bounds even a blocked Win32 read/log operation.
                // No content is accepted because elapsed time passed. Only our own process is terminated.
                TerminateProcess(GetCurrentProcess(), 4);
            }
        });
    }
    ~ProcessDeadline() { SetEvent(stop.value); if (watchdog.joinable()) watchdog.join(); }
    ProcessDeadline(const ProcessDeadline&) = delete;
    ProcessDeadline& operator=(const ProcessDeadline&) = delete;
};
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    if (s.size() > static_cast<size_t>(INT_MAX)) throw std::runtime_error("oversize string");
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("invalid UTF-16");
    std::string r(static_cast<size_t>(n), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr))
        throw std::runtime_error("UTF-16 conversion failed");
    return r;
}
inline std::string json(const std::string& s) {
    std::string r = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += static_cast<char>(c); }
        else if (c < 32) { r += "\\u00"; r += hex[c >> 4]; r += hex[c & 15]; }
        else r += static_cast<char>(c);
    }
    return r + '"';
}
inline std::wstring quote(const std::wstring& s) {
    std::wstring r = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        r.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') r += L'\\';
        r += c;
    }
    r.append(slashes * 2, L'\\');
    return r + L'"';
}
inline uint64_t utc100ns() {
    FILETIME f{}; GetSystemTimePreciseAsFileTime(&f);
    return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}
inline uint64_t qpc() { LARGE_INTEGER i{}; QueryPerformanceCounter(&i); return static_cast<uint64_t>(i.QuadPart); }
inline uint64_t frequency() { LARGE_INTEGER i{}; QueryPerformanceFrequency(&i); return static_cast<uint64_t>(i.QuadPart); }
inline std::string hash(const std::wstring& s) {
    const auto bytes = utf8(s);
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) throw std::runtime_error("SHA256 unavailable");
    unsigned char result[32]{};
    const NTSTATUS status = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), result, sizeof(result));
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) throw std::runtime_error("SHA256 failed");
    std::string r;
    const char* h = "0123456789abcdef";
    for (unsigned char c : result) { r += h[c >> 4]; r += h[c & 15]; }
    return r;
}
inline std::wstring compact(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) if (c != L' ' && c != L'\t' && c != L'\n' && c != L'\r') r += c;
    return r;
}
inline bool tokenValid(const std::wstring& s) {
    return !s.empty() && s.size() <= 80 && std::all_of(s.begin(), s.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    });
}
inline bool fixtureTokenPresent(const std::wstring& text, const std::wstring& token) {
    if (token.empty()) return false;
    size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find(L'\n', begin);
        if (compact(text.substr(begin, end == std::wstring::npos ? end : end - begin)) == token) return true;
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return false;
}
inline DWORD number(const std::wstring& s, DWORD min, DWORD max) {
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
        throw std::runtime_error("invalid numeric argument");
    const auto n = std::stoull(s);
    if (n < min || n > max) throw std::runtime_error("numeric argument outside bounds");
    return static_cast<DWORD>(n);
}
inline std::filesystem::path executable() {
    std::wstring b(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, b.data(), static_cast<DWORD>(b.size()));
    if (!n || n == b.size()) throw std::runtime_error("cannot get executable path");
    b.resize(n); return b;
}
inline std::wstring randomToken() {
    uint64_t v{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("random token generation failed");
    return L"SPK" + std::to_wstring(v);
}
struct Snapshot {
    DWORD sequence = 0, sequenceAfter = 0, ownerPid = 0, error = 0;
    std::vector<UINT> formats;
    std::wstring text;
    std::string status;
    bool matched = false;
};
inline Snapshot readClipboard(HWND hwnd, const std::wstring& expected) {
    Snapshot s;
    s.sequence = GetClipboardSequenceNumber();
    if (!OpenClipboard(hwnd)) { s.status = "clipboard_busy"; s.error = GetLastError(); return s; }
    struct Close { ~Close() { CloseClipboard(); } } close;
    s.sequence = GetClipboardSequenceNumber();
    HWND owner = GetClipboardOwner();
    if (owner) GetWindowThreadProcessId(owner, &s.ownerPid);
    UINT f = 0;
    while ((f = EnumClipboardFormats(f)) != 0) {
        s.formats.push_back(f);
        if (s.formats.size() >= 256) break;
    }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) s.status = "no_unicode_text";
    else {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (!data) { s.status = "read_failed"; s.error = GetLastError(); }
        else {
            SIZE_T size = GlobalSize(data);
            if (size < sizeof(wchar_t) || size > maxClipboardBytes || size % sizeof(wchar_t)) s.status = "invalid_size";
            else {
                const auto* p = static_cast<const wchar_t*>(GlobalLock(data));
                if (!p) { s.status = "lock_failed"; s.error = GetLastError(); }
                else {
                    struct Unlock { HANDLE h; ~Unlock() { GlobalUnlock(h); } } unlock{data};
                    const auto end = std::find(p, p + size / sizeof(wchar_t), L'\0');
                    if (end == p + size / sizeof(wchar_t)) s.status = "unterminated_text";
                    else { s.text.assign(p, end); s.status = s.text.empty() ? "empty_text" : "text"; }
                }
            }
        }
    }
    s.sequenceAfter = GetClipboardSequenceNumber();
    if (s.sequence != s.sequenceAfter) s.status = "sequence_changed_during_read";
    s.matched = s.status == "text" && fixtureTokenPresent(s.text, expected);
    return s;
}
inline const char* classify(const Snapshot& s, DWORD baseline, DWORD expectedOwner) {
    if (s.status != "text") return "unreadable_or_empty";
    if (!baseline || !s.sequence) return "sequence_unavailable";
    if (!s.matched) return "wrong_or_stale_text";
    if (s.sequence == baseline) return "baseline_not_changed";
    if (expectedOwner && s.ownerPid != expectedOwner) return "owner_mismatch";
    return "fixture_match_only"; // NOT a production provenance guarantee.
}
struct Timeline {
    bool launchMatched = false, eventualMatched = false, lateMismatch = false;
    double firstMatchMs = -1;
    void observe(const Snapshot& s, DWORD baseline, DWORD owner, bool hasBaseline, double ms, bool launchPhase) {
        const bool match = hasBaseline && std::string(classify(s, baseline, owner)) == "fixture_match_only";
        if (match && firstMatchMs < 0) firstMatchMs = ms;
        if (eventualMatched && !match && s.status != "clipboard_busy") lateMismatch = true;
        eventualMatched = eventualMatched || match;
        if (launchPhase) launchMatched = match;
    }
    int result() const { return launchMatched && !lateMismatch ? 0 : (eventualMatched && !lateMismatch ? 2 : 3); }
};
inline std::string metadata(const Snapshot& s) {
    std::string fs = "[";
    for (size_t i = 0; i < s.formats.size(); ++i) { if (i) fs += ','; fs += std::to_string(s.formats[i]); }
    fs += ']';
    return "\"sequence\":" + std::to_string(s.sequence) + ",\"sequence_after\":" + std::to_string(s.sequenceAfter) +
        ",\"owner_pid\":" + std::to_string(s.ownerPid) + ",\"formats\":" + fs + ",\"status\":" + json(s.status) +
        ",\"win32_error\":" + std::to_string(s.error) + ",\"utf16_units\":" + std::to_string(s.text.size()) +
        ",\"sha256\":" + json(s.text.empty() ? "" : hash(s.text)) + ",\"fixture_match\":" + (s.matched ? "true" : "false");
}
class Log {
    Handle file;
public:
    explicit Log(const std::filesystem::path& path) : file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create new log (exists or permission denied)");
    }
    void line(const std::string& s) {
        const std::string b = s + '\n'; DWORD written = 0;
        if (!WriteFile(file.value, b.data(), static_cast<DWORD>(b.size()), &written, nullptr) || written != b.size() || !FlushFileBuffers(file.value))
            throw std::runtime_error("cannot persist diagnostic log");
    }
};
inline HANDLE launch(const std::filesystem::path& app, const std::wstring& args) {
    std::wstring command = quote(app.wstring()) + L" " + args;
    if (command.size() >= 32767) throw std::runtime_error("command too long");
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        throw std::runtime_error("CreateProcess failed: " + std::to_string(GetLastError()));
    CloseHandle(pi.hThread); return pi.hProcess;
}
}
