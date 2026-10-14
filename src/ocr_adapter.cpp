#include "ocr_adapter.hpp"
#include <algorithm>
#include <vector>
#include <stdexcept>
#include <tlhelp32.h>

namespace sat {

// ---------------------------------------------------------------------------
// MockOcrAdapter
// ---------------------------------------------------------------------------

MockOcrAdapter::MockOcrAdapter(OcrSessionStatus presetStatus,
                               std::wstring presetText,
                               DWORD delayPolls)
    : presetStatus_(presetStatus),
      presetText_(std::move(presetText)),
      delayPolls_(delayPolls) {}

bool MockOcrAdapter::startCapture(DWORD timeoutMs, std::wstring& error) {
    if (timeoutMs == 0) {
        error = L"超时时间不能为 0";
        return false;
    }
    cancelCapture();
    ++sessionId_;
    currentPolls_ = 0;
    capturing_ = true;
    return true;
}

void MockOcrAdapter::cancelCapture() {
    capturing_ = false;
    ++sessionId_;
}

OcrResult MockOcrAdapter::poll() {
    if (!capturing_) {
        return {OcrSessionStatus::idle, {}, L"无正在进行的捕获会话"};
    }
    ++currentPolls_;
    if (currentPolls_ <= delayPolls_) {
        return {OcrSessionStatus::waiting, {}, L"Mock 等待中…"};
    }
    capturing_ = false;
    return {presetStatus_, presetText_, L"Mock 捕获结束", static_cast<DWORD>(sessionId_)};
}

void MockOcrAdapter::configure(OcrSessionStatus status, std::wstring text, DWORD delayPolls) {
    presetStatus_ = status;
    presetText_ = std::move(text);
    delayPolls_ = delayPolls;
    currentPolls_ = 0;
}

// ---------------------------------------------------------------------------
// SnipasteReverseAdapter
// ---------------------------------------------------------------------------

SnipasteReverseAdapter::SnipasteReverseAdapter(std::filesystem::path snipastePath)
    : snipastePath_(std::move(snipastePath)) {
    if (snipastePath_.empty()) {
        snipastePath_ = detectSnipasteExecutable();
    }
}

SnipasteReverseAdapter::~SnipasteReverseAdapter() {
    cleanupChildProcess();
}

void SnipasteReverseAdapter::cleanupChildProcess() noexcept {
    if (childProcess_) {
        // This may be the user's Snipaste application rather than a disposable helper.
        // Releasing our launch handle must not terminate their application.
        CloseHandle(childProcess_);
        childProcess_ = nullptr;
    }
}

std::filesystem::path SnipasteReverseAdapter::detectSnipasteExecutable() {
    // Explicit default for this installation. Never search Store aliases or PATH.
    const std::filesystem::path preferred=L"C:\\tools\\Snipaste\\Snipaste.exe";
    std::wstring error;return validateExecutable(preferred,error)?preferred:std::filesystem::path{};
}

bool SnipasteReverseAdapter::validateExecutable(const std::filesystem::path& executable, std::wstring& error) {
    error.clear();
    const auto value=executable.wstring();
    if(value.empty()||value.size()>32700||!executable.is_absolute()||
       value.find_first_of(L"\"\r\n")!=std::wstring::npos||value.find(L'\0')!=std::wstring::npos){
        error=L"请在设置中填写 OCR 来源 EXE 的完整绝对路径，不要附加引号或参数。";return false;
    }
    for(const auto& part:executable){
        auto component=part.wstring();std::transform(component.begin(),component.end(),component.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
        if(component==L"windowsapps"){error=L"当前 Snipaste OCR 来源不支持 WindowsApps 或 Store 别名；请指定有效 EXE。";return false;}
    }
    auto extension=executable.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    const DWORD attrs=GetFileAttributesW(executable.c_str());
    if(extension!=L".exe"||attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){
        error=L"OCR 来源 EXE 不存在、不可访问或不是普通 EXE 文件；请检查设置中的路径。";return false;
    }
    return true;
}

bool SnipasteReverseAdapter::isMeaningfulText(const std::wstring& text) noexcept {
    for (wchar_t ch : text) {
        if (!iswspace(ch) && ch != L'\0' && ch != 0xFEFF) {
            return true;
        }
    }
    return false;
}
std::wstring SnipasteReverseAdapter::captureCommand(const std::filesystem::path& executable){
    if(executable.empty()||executable.wstring().find_first_of(L"\"\r\n")!=std::wstring::npos||executable.wstring().find(L'\0')!=std::wstring::npos)throw std::runtime_error("invalid Snipaste executable path");
    return L"\""+executable.wstring()+L"\" snip -o ocr_clipboard";
}

SnipasteRunningStatus SnipasteReverseAdapter::runningInstance(const std::filesystem::path& executable){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return SnipasteRunningStatus::unverified;
    struct Closer{HANDLE h;~Closer(){CloseHandle(h);}}closeSnapshot{snapshot};
    PROCESSENTRY32W entry{sizeof(entry)};bool unknown=false;
    if(!Process32FirstW(snapshot,&entry))return SnipasteRunningStatus::unverified;
    do{
        if(_wcsicmp(entry.szExeFile,executable.filename().c_str())!=0)continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process){unknown=true;continue;}
        Closer closeProcess{process};wchar_t path[32768]{};DWORD length=static_cast<DWORD>(std::size(path));
        if(!QueryFullProcessImageNameW(process,0,path,&length)){unknown=true;continue;}
        std::error_code error;const bool same=std::filesystem::equivalent(executable,std::filesystem::path(path),error);
        if(same)return SnipasteRunningStatus::running;
        if(error)unknown=true;
    }while(Process32NextW(snapshot,&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES)unknown=true;
    return unknown?SnipasteRunningStatus::unverified:SnipasteRunningStatus::notRunning;
}

bool SnipasteReverseAdapter::startCapture(DWORD timeoutMs, std::wstring& error) {
    error.clear();
    // Repeated button/IPC requests must not cancel or restart a live capture.
    if(capturing_)return true;
    cleanupChildProcess();
    if(!validateExecutable(snipastePath_,error))return false;
    auto running=runningInstance(snipastePath_);
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(testRunningProbe_)running=testRunningProbe_();
#endif
    if(running==SnipasteRunningStatus::notRunning){
        error=L"指定的 Snipaste 专业版尚未运行。请先启动设置中的 EXE，等待托盘出现，再点击截图；不会回退 Store 版。";
        return false;
    }
    waitingDetail_=running==SnipasteRunningStatus::unverified?
        L"正在等待截图文字；无法核验专业版进程，请确认专业版已运行且权限一致（Esc 取消）。":
        L"正在等待截图文字：Snipaste 选区 / OCR（按 Esc 取消）…";

    ++sessionId_;
    baselineSequence_ = clipboardSequence();
    baselineText_.clear();

    startTime_ = GetTickCount64();
    timeoutMs_ = timeoutMs ? timeoutMs : 30000;

    {
        std::wstring command = captureCommand(snipastePath_);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const auto workingDirectory=snipastePath_.parent_path();
        if (CreateProcessW(snipastePath_.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            childProcess_ = pi.hProcess;
        } else {
            error = L"启动指定的 Snipaste EXE 失败（Win32 错误: " + std::to_wstring(GetLastError()) + L"）；未回退其他版本，请检查设置中的路径。";
            return false;
        }
    }

    capturing_ = true;
    return true;
}

void SnipasteReverseAdapter::cancelCapture() {
    capturing_ = false;
    ++sessionId_;
    cleanupChildProcess();
}

DWORD SnipasteReverseAdapter::clipboardSequence() const {
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(testSequence_)return testSequence_();
#endif
    return GetClipboardSequenceNumber();
}

ClipboardReadStatus SnipasteReverseAdapter::readClipboardText(std::wstring& outText, DWORD& outSeq) {
#if defined(SAT_ENABLE_TEST_HOOKS)
    if(testReader_)return testReader_(outText,outSeq);
#endif
    if (!OpenClipboard(nullptr)) {
        return ClipboardReadStatus::retry;
    }
    struct ClipboardCloser { ~ClipboardCloser() { CloseClipboard(); } } closer;
    outSeq = GetClipboardSequenceNumber();

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return ClipboardReadStatus::ignored;
    }

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) return ClipboardReadStatus::retry;

    const SIZE_T bytes = GlobalSize(h);
    if (bytes < sizeof(wchar_t) || bytes > 1024 * 1024 || (bytes % sizeof(wchar_t)) != 0) {
        return ClipboardReadStatus::ignored;
    }

    const auto* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (!p) return ClipboardReadStatus::retry;

    struct Unlocker { HANDLE h; ~Unlocker() { GlobalUnlock(h); } } unlocker{h};
    const auto end = std::find(p, p + bytes / sizeof(wchar_t), L'\0');
    if (end == p + bytes / sizeof(wchar_t)) {
        return ClipboardReadStatus::ignored;
    }

    std::wstring extracted(p, end);
    if (!isMeaningfulText(extracted)) {
        return ClipboardReadStatus::ignored;
    }

    outText = std::move(extracted);
    return ClipboardReadStatus::text;
}

OcrResult SnipasteReverseAdapter::poll() {
    if (!capturing_) {
        return {OcrSessionStatus::idle, {}, L"未在等待截图"};
    }

    // Check bounded timeout
    if (GetTickCount64() - startTime_ >= timeoutMs_) {
        cancelCapture();
        return {OcrSessionStatus::timeout, {}, L"等待截图 OCR 超时。"};
    }

    DWORD currentSeq = clipboardSequence();
    if (currentSeq != 0 && currentSeq != baselineSequence_) {
        std::wstring text;
        DWORD readSeq = 0;
        const auto readStatus=readClipboardText(text, readSeq);
        if(readStatus==ClipboardReadStatus::retry||readSeq==0)
            return {OcrSessionStatus::waiting,{},L"剪贴板暂不可读，继续等待 OCR（按 Esc 取消）。",0,true};
        if (readStatus==ClipboardReadStatus::text) {
            if(readSeq==baselineSequence_)return {OcrSessionStatus::waiting,{},L"剪贴板尚未更新。"};
            // Keep this capture's original deadline. Rejecting an unrelated copy
            // must not relaunch Snipaste or lose a subsequent OCR update.
            baselineSequence_=readSeq;
            return {OcrSessionStatus::candidate, std::move(text), L"剪贴板已更新，但无法确认来自本次截图。", readSeq};
        }
        // Sequence changed but clipboard content is non-text or empty/whitespace:
        // Update baseline to prevent spinning on non-text changes, and continue waiting.
        baselineSequence_ = readSeq;
    }

    return {OcrSessionStatus::waiting, {}, waitingDetail_};
}

bool SnipasteReverseAdapter::isCandidateCurrent(DWORD sequence) const {
    return sequence != 0 && clipboardSequence() == sequence;
}

bool SnipasteReverseAdapter::hasClipboardUpdate() const {
    if(!capturing_)return false;
    const DWORD sequence=clipboardSequence();
    return sequence!=0&&sequence!=baselineSequence_;
}

// ---------------------------------------------------------------------------
// SnipasteForwardAdapter
// ---------------------------------------------------------------------------

bool SnipasteForwardAdapter::startCapture(DWORD /*timeoutMs*/, std::wstring& error) {
    error = L"正向集成方案当前保持 HOLD；推荐使用反向集成方案。";
    capturing_ = false;
    return false;
}

void SnipasteForwardAdapter::cancelCapture() {
    capturing_ = false;
}

OcrResult SnipasteForwardAdapter::poll() {
    return {OcrSessionStatus::idle, {}, L"正向适配器未激活"};
}

std::wstring SnipasteForwardAdapter::generateCommandLine(
    const std::filesystem::path& probeExe,
    const std::filesystem::path& logPath,
    const std::wstring& token,
    DWORD baselineSeq,
    DWORD timeoutMs) {
    return L"\"" + probeExe.wstring() + L"\" --log \"" + logPath.wstring() +
           L"\" --expect " + token +
           L" --baseline " + std::to_wstring(baselineSeq) +
           L" --timeout-ms " + std::to_wstring(timeoutMs);
}

} // namespace sat
