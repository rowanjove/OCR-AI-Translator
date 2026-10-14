#pragma once
#include <windows.h>
#include <string>
#include <memory>
#include <filesystem>
#include <functional>

namespace sat {

enum class OcrAdapterKind {
    none,
    mock,
    reverseSnipaste,
    forwardSnipaste
};

enum class OcrSessionStatus {
    idle,
    waiting,
    captured,
    timeout,
    cancelled,
    failed
    ,candidate // Clipboard update with unverified provenance; requires explicit confirmation.
};

struct OcrResult {
    OcrSessionStatus status = OcrSessionStatus::idle;
    std::wstring text;
    std::wstring detail;
    DWORD sequence = 0;
    bool retrySoon = false;
};

enum class ClipboardReadStatus { retry, ignored, text };
enum class SnipasteRunningStatus { running, notRunning, unverified };

class IOcrAdapter {
public:
    virtual ~IOcrAdapter() = default;
    virtual OcrAdapterKind kind() const noexcept = 0;
    virtual bool startCapture(DWORD timeoutMs, std::wstring& error) = 0;
    virtual void cancelCapture() = 0;
    virtual OcrResult poll() = 0;
    virtual bool isCapturing() const noexcept = 0;
    virtual uint64_t sessionId() const noexcept = 0;
    virtual bool isCandidateCurrent(DWORD sequence) const { return sequence != 0; }
};

// Mock OCR adapter for deterministic unit and process tests
class MockOcrAdapter : public IOcrAdapter {
public:
    explicit MockOcrAdapter(OcrSessionStatus presetStatus = OcrSessionStatus::captured,
                            std::wstring presetText = L"Mock OCR Result",
                            DWORD delayPolls = 0);

    OcrAdapterKind kind() const noexcept override { return OcrAdapterKind::mock; }
    bool startCapture(DWORD timeoutMs, std::wstring& error) override;
    void cancelCapture() override;
    OcrResult poll() override;
    bool isCapturing() const noexcept override { return capturing_; }
    uint64_t sessionId() const noexcept override { return sessionId_; }
    bool isCandidateCurrent(DWORD sequence) const override { return sequence != 0; }

    void configure(OcrSessionStatus status, std::wstring text, DWORD delayPolls = 0);
    DWORD pollCount() const noexcept { return currentPolls_; }

private:
    bool capturing_ = false;
    uint64_t sessionId_ = 0;
    OcrSessionStatus presetStatus_;
    std::wstring presetText_;
    DWORD delayPolls_ = 0;
    DWORD currentPolls_ = 0;
};

// Reverse integration: Translator invokes Snipaste screenshot/OCR,
// then observes clipboard changes to receive text.
class SnipasteReverseAdapter : public IOcrAdapter {
public:
    explicit SnipasteReverseAdapter(std::filesystem::path snipastePath = {});
    ~SnipasteReverseAdapter() override;

    OcrAdapterKind kind() const noexcept override { return OcrAdapterKind::reverseSnipaste; }
    bool startCapture(DWORD timeoutMs, std::wstring& error) override;
    void cancelCapture() override;
    OcrResult poll() override;
    bool isCapturing() const noexcept override { return capturing_; }
    uint64_t sessionId() const noexcept override { return sessionId_; }
    bool isCandidateCurrent(DWORD sequence) const override;
    bool hasClipboardUpdate() const;

    void setSnipastePath(std::filesystem::path p) { snipastePath_ = std::move(p); }
    const std::filesystem::path& snipastePath() const noexcept { return snipastePath_; }

    DWORD baselineSequence() const noexcept { return baselineSequence_; }
    const std::wstring& baselineText() const noexcept { return baselineText_; }

    static std::filesystem::path detectSnipasteExecutable();
    static bool validateExecutable(const std::filesystem::path& executable, std::wstring& error);
    static std::wstring captureCommand(const std::filesystem::path& executable);
    static bool isMeaningfulText(const std::wstring& text) noexcept;
    static SnipasteRunningStatus runningInstance(const std::filesystem::path& executable);
#if defined(SAT_ENABLE_TEST_HOOKS)
    void setRunningProbeForTest(std::function<SnipasteRunningStatus()> probe) { testRunningProbe_=std::move(probe); }
    void setClipboardReaderForTest(std::function<DWORD()> sequence,
        std::function<ClipboardReadStatus(std::wstring&, DWORD&)> reader) {
        testSequence_ = std::move(sequence); testReader_ = std::move(reader);
    }
#endif

private:
    std::filesystem::path snipastePath_;
    bool capturing_ = false;
    uint64_t sessionId_ = 0;
    DWORD baselineSequence_ = 0;
    std::wstring baselineText_;
    ULONGLONG startTime_ = 0;
    DWORD timeoutMs_ = 30000;
    std::wstring waitingDetail_;
    HANDLE childProcess_ = nullptr;

    void cleanupChildProcess() noexcept;
    DWORD clipboardSequence() const;
    ClipboardReadStatus readClipboardText(std::wstring& outText, DWORD& outSeq);
#if defined(SAT_ENABLE_TEST_HOOKS)
    std::function<SnipasteRunningStatus()> testRunningProbe_;
    std::function<DWORD()> testSequence_;
    std::function<ClipboardReadStatus(std::wstring&, DWORD&)> testReader_;
#endif
};

// Forward integration adapter placeholder: preserves legacy command-line and bridge options
class SnipasteForwardAdapter : public IOcrAdapter {
public:
    SnipasteForwardAdapter() = default;

    OcrAdapterKind kind() const noexcept override { return OcrAdapterKind::forwardSnipaste; }
    bool startCapture(DWORD timeoutMs, std::wstring& error) override;
    void cancelCapture() override;
    OcrResult poll() override;
    bool isCapturing() const noexcept override { return capturing_; }
    uint64_t sessionId() const noexcept override { return 0; }

    static std::wstring generateCommandLine(const std::filesystem::path& probeExe,
                                            const std::filesystem::path& logPath,
                                            const std::wstring& token,
                                            DWORD baselineSeq,
                                            DWORD timeoutMs);
private:
    bool capturing_ = false;
};

} // namespace sat
