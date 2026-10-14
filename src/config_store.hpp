#pragma once
#include "json.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <vector>
#include <string>

namespace sat {
enum class Theme { system, light, dark };
enum class ThinkingMode : uint8_t { automatic = 0, disabled = 1, enabled = 2 };
enum class ThinkingProtocol : uint8_t { unsupported = 0, deepSeekChatCompletions = 1 };

ThinkingProtocol detectThinkingProtocol(std::wstring_view baseUrl, std::wstring_view model) noexcept;
ThinkingMode defaultThinkingMode(std::wstring_view baseUrl, std::wstring_view model) noexcept;

struct ProviderProfile {
    std::wstring id = L"default";
    std::wstring name = L"默认模型";
    std::wstring baseUrl;
    std::wstring model;
    std::wstring apiKey;
    std::wstring credentialRef;
    unsigned timeoutSeconds = 60;
    unsigned maxOutputTokens = 2048;
    ThinkingMode thinkingMode = ThinkingMode::automatic;
    bool thinkingModeExplicit = false; // Runtime marker; only the string setting is serialized.
    bool enabled = true;
    bool reuseCredential = false;
};

struct Config {
    uint32_t schemaVersion = 1;
    std::wstring targetLanguage = L"zh-Hans";
    std::wstring snipasteExePath = L"C:\\tools\\Snipaste\\Snipaste.exe";
    Theme theme = Theme::system;
    int widthDip = 850, heightDip = 380, xDip = 0, yDip = 0;
    bool rememberPosition = false, alwaysOnTop = false, showSourceCopy = true;
    bool showUnlock = true, showRestore = true, showResultCopy = true, showRetranslate = true;
    bool showRewrite = true, showDeAi = true, showCustomPrompt = true, showVersionHistory = true;
    bool autoStart = false;
    unsigned versionLimit = 10;
    bool rememberPrompts = true;
    bool confirmOcrCandidates = true;
    std::vector<int> toolbarOrder{117,103,105,106,107,108,111,104,118,119,120,121};

    std::vector<ProviderProfile> profiles;
    std::wstring translationProfileId = L"default";
    std::wstring rewriteProfileId = L"default";

    std::wstring baseUrl, model, apiKey;
    unsigned timeoutSeconds = 60, maxOutputTokens = 2048;
    std::wstring credentialRef;
    json::Value::Object unknownRoot;
    std::filesystem::path folder;
    std::wstring warning;
    bool damagedConfig = false, reuseCredential = false;

    // Runtime-only compatibility state.  Legacy callers still edit the flat
    // active fields, while profile-aware callers edit profiles directly.
    // Keep a small snapshot so saveConfig can distinguish those two cases.
    bool activeSnapshotValid = false;
    std::wstring activeSnapshotProfileId;
    std::wstring activeSnapshotBaseUrl;
    std::wstring activeSnapshotModel;
    std::wstring activeSnapshotCredentialRef;
    unsigned activeSnapshotTimeoutSeconds = 60;
    unsigned activeSnapshotMaxOutputTokens = 2048;
    bool activeSnapshotReuseCredential = false;
    uint64_t activeSnapshotApiKeyFingerprint = 0;
    size_t activeSnapshotApiKeyLength = 0;

    const ProviderProfile* findProfile(const std::wstring& id) const noexcept;
    ProviderProfile* findProfile(const std::wstring& id) noexcept;
    const ProviderProfile& getTranslationProfile() const noexcept;
    const ProviderProfile& getRewriteProfile() const noexcept;
    const ProviderProfile* getProfileForRole(const std::wstring& role) const noexcept {
        if(role!=L"translate"&&role!=L"translation"&&role!=L"rewrite")return nullptr;
        const auto* profile=findProfile((role == L"translate" || role == L"translation")?translationProfileId:rewriteProfileId);
        return profile&&profile->enabled?profile:nullptr;
    }
    void syncActiveFromProfiles() noexcept;
    void syncProfilesFromActive();
};
std::filesystem::path configFolder(bool create = true);
Config loadConfig(bool createFolder = true);
bool saveConfig(Config& candidate, std::wstring& error);
void clearSecret(std::wstring& value) noexcept;
void clearConfigSecrets(Config& config) noexcept;
bool validateEndpoint(const std::wstring& baseUrl, std::wstring& error);
}
