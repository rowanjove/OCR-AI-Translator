#include "sse_parser.hpp"
#include "translation_client.hpp"

namespace sat {
bool isSafeTranslationText(std::wstring_view text) noexcept {
    for(const wchar_t ch:text)if(ch==L'\0'||(ch<0x20&&ch!=L'\r'&&ch!=L'\n'&&ch!=L'\t'))return false;
    return true;
}
namespace {
std::wstring contentFromDelta(std::string_view payload) {
    const auto root = json::Parser(payload).parse();
    const auto* choicesValue = root.get("choices");
    const auto* choices = choicesValue ? choicesValue->array() : nullptr;
    if (!choices || choices->empty()) return {};
    if(const auto* reason=choices->front().get("finish_reason")){
        if(const auto* text=reason->string();text&&!text->empty()&&*text!="stop"){
            TranslationErrorKind kind=TranslationErrorKind::incompatibleResponse;
            if(*text=="length")kind=TranslationErrorKind::outputTruncated;
            else if(*text=="content_filter")kind=TranslationErrorKind::contentFiltered;
            throw TranslationFailure(kind,safeFinishReasonCode(*text));
        }
        if(!reason->string()&&!std::holds_alternative<std::nullptr_t>(reason->data))throw TranslationFailure(TranslationErrorKind::incompatibleResponse,L"finish_reason=invalid_type");
    }
    const auto* deltaValue = choices->front().get("delta");
    const auto* delta = deltaValue ? deltaValue->object() : nullptr;
    const auto* contentValue = delta ? deltaValue->get("content") : nullptr;
    const auto* content = contentValue ? contentValue->string() : nullptr;
    if(!content)return {};
    auto decoded=json::decode(*content);if(!isSafeTranslationText(decoded))throw std::runtime_error("translation contains unsupported control characters");return decoded;
}
}
void SseParser::feed(std::string_view bytes) {
    if (done_) return;
    if (bytes.size() > 4 * 1024 * 1024 - totalBytes_) throw std::runtime_error("SSE response exceeds size limit");
    totalBytes_ += bytes.size();
    for (char c : bytes) {
        if (c == '\n' && lastWasCr_) { lastWasCr_ = false; continue; }
        lastWasCr_ = c == '\r';
        if (c == '\n' || c == '\r') {
            line(pendingLine_);
            pendingLine_.clear();
            if (done_) return;
        } else {
            pendingLine_.push_back(c);
            if (pendingLine_.size() > 1024 * 1024) throw std::runtime_error("SSE event line exceeds size limit");
        }
    }
}
void SseParser::finish() {
    if (done_) return;
    // EOF is not an event delimiter: discard any incomplete final event.
    pendingLine_.clear();
    eventData_.clear();
}
void SseParser::line(std::string_view value) {
    if (firstLine_) { firstLine_ = false; if (value.starts_with("\xef\xbb\xbf")) value.remove_prefix(3); }
    if (value.empty()) { dispatch(); return; }
    if (value.front() == ':') return;
    const auto colon = value.find(':');
    const auto field = value.substr(0, colon);
    if (field != "data") return;
    std::string_view data = colon == std::string_view::npos ? std::string_view{} : value.substr(colon + 1);
    if (!data.empty() && data.front() == ' ') data.remove_prefix(1);
    if (!eventData_.empty()) eventData_.push_back('\n');
    eventData_.append(data);
    if (eventData_.size() > 1024 * 1024) throw std::runtime_error("SSE event exceeds size limit");
}
void SseParser::dispatch() {
    if (eventData_.empty()) return;
    if (eventData_ == "[DONE]") { done_ = true; eventData_.clear(); return; }
    const auto delta = contentFromDelta(eventData_);
    if (!delta.empty() && content_) content_(delta);
    eventData_.clear();
}
}
