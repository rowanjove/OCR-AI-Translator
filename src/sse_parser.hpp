#pragma once
#include "json.hpp"
#include <functional>
#include <string_view>

namespace sat {
bool isSafeTranslationText(std::wstring_view text) noexcept;
class SseParser {
public:
    using Content = std::function<void(std::wstring_view)>;
    explicit SseParser(Content content) : content_(std::move(content)) {}
    void feed(std::string_view bytes);
    void finish();
    bool done() const noexcept { return done_; }
private:
    void line(std::string_view value);
    void dispatch();
    Content content_;
    std::string pendingLine_, eventData_;
    bool done_ = false;
    bool lastWasCr_ = false, firstLine_ = true;
    size_t totalBytes_ = 0;
};
}
