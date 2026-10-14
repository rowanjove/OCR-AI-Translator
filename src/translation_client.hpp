#pragma once
#include "config_store.hpp"
#include "json.hpp"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace sat {
enum class AiOperationType : uint8_t { translate = 0, rewrite = 1, naturalize = 2, custom = 3 };
enum class RewriteStyle : uint8_t { natural = 0, concise = 1, casual = 2, formal = 3, literary = 4 };
enum class CustomPromptScope : uint8_t { sourceAndResult = 0, resultOnly = 1, sourceOnly = 2 };
enum class TranslationPhase : uint8_t { setup, send, headers, body, complete };
enum class TranslationErrorKind : uint8_t { none, outputTruncated, contentFiltered, networkInterrupted, httpStatus, incompatibleResponse, noTranslationContent, requestFailed };

class TranslationFailure final : public std::runtime_error {
public:
    TranslationFailure(TranslationErrorKind kind, std::wstring code);
    TranslationErrorKind kind;
    std::wstring code;
};
const wchar_t* translationErrorMessage(TranslationErrorKind kind) noexcept;
inline std::wstring safeFinishReasonCode(std::string_view reason){
    // Provider-controlled strings must not become diagnostic text.
    if(reason=="length")return L"finish_reason=length";
    if(reason=="content_filter")return L"finish_reason=content_filter";
    if(reason=="tool_calls")return L"finish_reason=tool_calls";
    if(reason=="function_call")return L"finish_reason=function_call";
    return L"finish_reason=unknown";
}

struct TranslationRequest {
    std::wstring baseUrl, model, apiKey, text, targetLanguage;
    unsigned timeoutSeconds = 60, maxOutputTokens = 2048;
    ThinkingMode thinkingMode = ThinkingMode::automatic;
    uint64_t requestId = 0, revision = 0, sourceSessionId = 0;
    bool testConnection = false;

    AiOperationType opType = AiOperationType::translate;
    RewriteStyle rewriteStyle = RewriteStyle::natural;
    CustomPromptScope customScope = CustomPromptScope::sourceAndResult;
    std::wstring customPrompt;
    std::wstring resultText;
    std::wstring modelProfileId;
    uint64_t baseVersionId = 0;
    // Application-level POST cap; 2 preserves the existing stream fallback.
    unsigned maxNetworkRequests = 2;
};
struct TranslationEvent {
    enum class Kind { delta, complete, error, cancelled } kind;
    uint64_t requestId = 0, revision = 0;
    bool testConnection = false;
    std::wstring text;
    uint64_t sourceSessionId = 0;

    AiOperationType opType = AiOperationType::translate;
    RewriteStyle rewriteStyle = RewriteStyle::natural;
    CustomPromptScope customScope = CustomPromptScope::sourceAndResult;
    std::wstring customPrompt;
    std::wstring modelProfileId;
    uint64_t baseVersionId = 0;
    DWORD httpStatus = 0, networkError = 0;
    unsigned networkRequests = 0;
    bool responseIsStream = false, streamFallbackBlocked = false;
    TranslationPhase phase = TranslationPhase::setup;
    TranslationErrorKind errorKind = TranslationErrorKind::none;
    std::wstring errorCode;
    unsigned maxOutputTokens = 0;
    ThinkingMode thinkingMode = ThinkingMode::automatic;
};
inline bool isCurrentTranslationEvent(const TranslationEvent& event,uint64_t activeRequestId,uint64_t activeRevision,bool testConnection,uint64_t sourceSessionId=0) noexcept {
    return event.testConnection==testConnection&&event.requestId==activeRequestId&&(testConnection||(event.revision==activeRevision&&event.sourceSessionId==sourceSessionId));
}
class TranslationTask {
public:
    struct State;
    using Callback = std::function<void(TranslationEvent)>;
    using FinishedCallback = std::function<void()>;
    static std::shared_ptr<TranslationTask> start(TranslationRequest request, Callback callback, FinishedCallback finishedCallback = {});
    ~TranslationTask();
    TranslationTask(const TranslationTask&) = delete;
    TranslationTask& operator=(const TranslationTask&) = delete;
    void cancel() noexcept;
    // Calling join from the worker callback detaches; the captured State owns cleanup.
    void join() noexcept;
    bool finished() const noexcept;
private:
    explicit TranslationTask(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
    std::thread worker_;
};
json::Value chatBody(const TranslationRequest& r, bool stream);
std::wstring httpError(DWORD status);
}
