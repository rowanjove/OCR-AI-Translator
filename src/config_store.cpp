#include "config_store.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <shlobj.h>
#include <dpapi.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cwctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace sat {
ThinkingProtocol detectThinkingProtocol(std::wstring_view baseUrl,std::wstring_view model) noexcept {
    if(model!=L"deepseek-flash"||baseUrl.empty()||baseUrl.size()>2048)return ThinkingProtocol::unsupported;
    try {
        const std::wstring url(baseUrl);
        URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwSchemeLength=static_cast<DWORD>(-1);parts.dwHostNameLength=static_cast<DWORD>(-1);parts.dwUserNameLength=static_cast<DWORD>(-1);parts.dwPasswordLength=static_cast<DWORD>(-1);parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=INTERNET_DEFAULT_HTTPS_PORT||parts.dwUserNameLength||parts.dwPasswordLength||parts.dwExtraInfoLength||!parts.lpszHostName||parts.dwHostNameLength!=16)return ThinkingProtocol::unsupported;
        std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
        std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
        std::wstring path=parts.lpszUrlPath&&parts.dwUrlPathLength?std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength):L"/";
        while(path.size()>1&&path.back()==L'/')path.pop_back();
        const bool chatCompletionsPath=path==L"/"||path==L"/v1"||path==L"/chat/completions"||path==L"/v1/chat/completions";
        return host==L"api.deepseek.com"&&chatCompletionsPath?ThinkingProtocol::deepSeekChatCompletions:ThinkingProtocol::unsupported;
    } catch(...) { return ThinkingProtocol::unsupported; }
}
ThinkingMode defaultThinkingMode(std::wstring_view baseUrl,std::wstring_view model) noexcept {
    return detectThinkingProtocol(baseUrl,model)==ThinkingProtocol::deepSeekChatCompletions?ThinkingMode::disabled:ThinkingMode::automatic;
}
namespace {
constexpr size_t maxConfigBytes = 1024 * 1024;
constexpr size_t maxProfiles = 32;
constexpr size_t maxProfileIdChars = 64;
constexpr size_t maxProfileNameChars = 64;
constexpr size_t maxModelChars = 256;
constexpr size_t maxApiKeyChars = 4096;
constexpr unsigned minVersionLimit = 1;
constexpr unsigned maxVersionLimit = 50;
constexpr int toolbarActionIds[] = {117,103,105,106,107,108,111,104,118,119,120,121};
constexpr wchar_t entropyText[] = L"SnipasteAITranslator/v1/credentials";
void atomicWrite(const std::filesystem::path& path, const void* data, size_t size) {
    if(size>maxConfigBytes)throw std::runtime_error("file exceeds size limit");
    unsigned char random[12]{}; if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("temporary name generation failed");
    static constexpr wchar_t hex[]=L"0123456789abcdef"; std::wstring suffix=L".tmp-";for(auto b:random){suffix+=hex[b>>4];suffix+=hex[b&15];}
    const auto temp=std::filesystem::path(path.wstring()+suffix);
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot create temporary config file");
    bool keep=false;
    try {
        size_t pos=0;while(pos<size){DWORD chunk=static_cast<DWORD>(std::min<size_t>(size-pos,64*1024)),written=0;if(!WriteFile(h,static_cast<const char*>(data)+pos,chunk,&written,nullptr)||written!=chunk)throw std::runtime_error("config write failed");pos+=written;}
        if(!FlushFileBuffers(h))throw std::runtime_error("config flush failed");CloseHandle(h);h=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("atomic config replace failed");keep=true;
    }catch(...){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);if(!keep)DeleteFileW(temp.c_str());throw;}
}
bool cleanupOrphanCredentials(const std::filesystem::path& folder,const std::vector<std::wstring>& keepRefs) noexcept {
    try {
        const auto pattern=(folder/L"secret-*.bin").wstring();WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW(pattern.c_str(),&data);
        if(search==INVALID_HANDLE_VALUE){const DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;}
        bool cleaned=true;
        do {
            if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
            const std::wstring name=data.cFileName;
            if(name.size()!=43||name.compare(0,7,L"secret-")!=0||name.compare(39,4,L".bin")!=0)continue;
            const std::wstring ref=name.substr(7,32);
            if(!std::all_of(ref.begin(),ref.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})||std::find(keepRefs.begin(),keepRefs.end(),ref)!=keepRefs.end())continue;
            if(!DeleteFileW((folder/name).c_str())){const DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)cleaned=false;}
        } while(FindNextFileW(search,&data));
        const DWORD enumerationError=GetLastError();const BOOL closed=FindClose(search);
        return cleaned&&enumerationError==ERROR_NO_MORE_FILES&&closed;
    } catch(...) {return false;}
}
std::string readUtf8File(const std::filesystem::path& p) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot read config file");
    LARGE_INTEGER n{};if(!GetFileSizeEx(h,&n)||n.QuadPart<0||static_cast<uint64_t>(n.QuadPart)>maxConfigBytes){CloseHandle(h);throw std::runtime_error("config size is invalid");}
    std::string s(static_cast<size_t>(n.QuadPart),'\0');size_t pos=0;while(pos<s.size()){DWORD got=0;if(!ReadFile(h,s.data()+pos,static_cast<DWORD>(std::min<size_t>(s.size()-pos,64*1024)),&got,nullptr)||!got){CloseHandle(h);throw std::runtime_error("config read failed");}pos+=got;}CloseHandle(h);return s;
}
std::string u8(std::wstring_view s){return json::encode(s);}
void wipeJsonStrings(json::Value& value) noexcept {
    if(auto* text=std::get_if<std::string>(&value.data)){if(!text->empty())SecureZeroMemory(text->data(),text->size());}
    else if(auto* object=std::get_if<json::Value::Object>(&value.data)){for(auto& item:*object)wipeJsonStrings(item.second);}
    else if(auto* array=std::get_if<json::Value::Array>(&value.data)){for(auto& item:*array)wipeJsonStrings(item);}
}
void stripSensitive(json::Value& value) {
    if (auto* object = std::get_if<json::Value::Object>(&value.data)) {
        for (auto it=object->begin();it!=object->end();) {
            std::string key=it->first;std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(key.find("apikey")!=std::string::npos||key.find("api_key")!=std::string::npos||key.find("api-key")!=std::string::npos||key.find("authorization")!=std::string::npos||key.find("access_token")!=std::string::npos||key.find("refresh_token")!=std::string::npos||key.find("bearer_token")!=std::string::npos||key.find("secret")!=std::string::npos||(key.find("credential")!=std::string::npos&&key!="credential_ref")){wipeJsonStrings(it->second);it=object->erase(it);}
            else {stripSensitive(it->second);++it;}
        }
    } else if (auto* array=std::get_if<json::Value::Array>(&value.data)) for(auto& item:*array)stripSensitive(item);
}
std::wstring field(const json::Value& root,const char* key,std::wstring fallback,size_t max=4096){const auto*v=root.get(key);if(!v)return fallback;const auto*s=v->string();if(!s)throw std::runtime_error("invalid config string");auto w=json::decode(*s);if(w.size()>max)throw std::runtime_error("config field exceeds limit");return w;}
const char* thinkingModeName(ThinkingMode mode) noexcept {switch(mode){case ThinkingMode::disabled:return "disabled";case ThinkingMode::enabled:return "enabled";case ThinkingMode::automatic:default:return "automatic";}}
ThinkingMode thinkingModeField(const json::Value& root,const char* key,ThinkingMode fallback){const auto* value=root.get(key);if(!value)return fallback;const auto* text=value->string();if(!text)throw std::runtime_error("invalid thinking mode");if(*text=="automatic")return ThinkingMode::automatic;if(*text=="disabled")return ThinkingMode::disabled;if(*text=="enabled")return ThinkingMode::enabled;throw std::runtime_error("invalid thinking mode");}
bool boolField(const json::Value& root,const char* key,bool fallback){const auto*v=root.get(key);if(!v)return fallback;const auto*b=v->boolean();if(!b)throw std::runtime_error("invalid config boolean");return *b;}
int intField(const json::Value& root,const char* key,int fallback,int min,int max){const auto*v=root.get(key);if(!v)return fallback;const auto*n=v->number();if(!n||*n<min||*n>max||std::floor(*n)!=*n)throw std::runtime_error("invalid config number");return static_cast<int>(*n);}
uint64_t apiKeyFingerprint(std::wstring_view value) noexcept {
    // This is only a change detector; the key itself is never retained in
    // the snapshot.  The length is checked separately to make the marker
    // cheap and deterministic without adding another secret copy to Config.
    uint64_t hash=1469598103934665603ull;
    for(const auto ch:value){hash^=static_cast<uint64_t>(ch);hash*=1099511628211ull;}
    return hash;
}
bool isCredentialReference(const std::wstring& ref) noexcept {
    return ref.size()==32&&std::all_of(ref.begin(),ref.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});
}
bool isToolbarActionId(int id) noexcept {
    return std::find(std::begin(toolbarActionIds),std::end(toolbarActionIds),id)!=std::end(toolbarActionIds);
}
void validateToolbarOrder(const std::vector<int>& order) {
    if(order.size()!=std::size(toolbarActionIds))throw std::runtime_error("toolbar order must contain all actions");
    std::set<int> seen;
    for(const int id:order)if(!isToolbarActionId(id)||!seen.insert(id).second)throw std::runtime_error("toolbar order contains an invalid or duplicate action");
}
std::vector<int> toolbarOrderField(const json::Value& root,const char* key,const std::vector<int>& fallback){
    const auto* value=root.get(key);if(!value)return fallback;
    const auto* array=value->array();if(!array)throw std::runtime_error("invalid toolbar order");
    if(array->size()!=std::size(toolbarActionIds))throw std::runtime_error("invalid toolbar order");
    std::vector<int> result;result.reserve(array->size());
    for(const auto& item:*array){
        const auto* number=item.number();
        if(!number||!std::isfinite(*number)||std::floor(*number)!=*number||*number<INT_MIN||*number>INT_MAX)throw std::runtime_error("invalid toolbar order");
        result.push_back(static_cast<int>(*number));
    }
    validateToolbarOrder(result);return result;
}
void validateProfileCollection(const Config& config) {
    if(config.profiles.size()>maxProfiles)throw std::runtime_error("too many provider profiles");
    std::set<std::wstring> ids;
    for(const auto& profile:config.profiles){
        if(profile.id.empty()||profile.id.size()>maxProfileIdChars)throw std::runtime_error("invalid provider profile id");
        if(!ids.insert(profile.id).second)throw std::runtime_error("duplicate provider profile id");
        if(profile.name.size()>maxProfileNameChars)throw std::runtime_error("provider profile name exceeds limit");
        if(profile.model.size()>maxModelChars)throw std::runtime_error("provider model exceeds size limit");
        if(profile.apiKey.size()>maxApiKeyChars)throw std::runtime_error("provider API key exceeds size limit");
        for(wchar_t c:profile.apiKey)if(c<0x21||c>0x7e)throw std::runtime_error("provider API key contains invalid header characters");
        if(profile.credentialRef.size()>32||(!profile.credentialRef.empty()&&!isCredentialReference(profile.credentialRef)))throw std::runtime_error("invalid credential reference");
        if(profile.timeoutSeconds<5||profile.timeoutSeconds>180||profile.maxOutputTokens<1||profile.maxOutputTokens>32768)throw std::runtime_error("provider limits are invalid");
        if(profile.thinkingMode>ThinkingMode::enabled)throw std::runtime_error("provider thinking mode is invalid");
        if(!profile.baseUrl.empty()){
            std::wstring endpointError;
            if(!validateEndpoint(profile.baseUrl,endpointError))throw std::runtime_error("invalid endpoint in provider profile");
        }
    }
    if(config.translationProfileId.empty()||config.rewriteProfileId.empty())throw std::runtime_error("provider role id is empty");
    if(!config.findProfile(config.translationProfileId)||!config.findProfile(config.rewriteProfileId))throw std::runtime_error("provider role references missing profile");
}
void validateConfigFields(const Config& config) {
    if(config.snipasteExePath.size()>32700||config.snipasteExePath.find_first_of(L"\"\r\n")!=std::wstring::npos||config.snipasteExePath.find(L'\0')!=std::wstring::npos)throw std::runtime_error("invalid Snipaste path");
    if(config.versionLimit<minVersionLimit||config.versionLimit>maxVersionLimit)throw std::runtime_error("version limit is invalid");
    validateToolbarOrder(config.toolbarOrder);
}
std::wstring protect(const std::wstring& secret,const std::filesystem::path& folder){
    if(secret.empty())return {};
    if(secret.size()>maxApiKeyChars)throw std::runtime_error("API key exceeds size limit");
    std::string bytes=json::encode(secret);struct PlainWipe{std::string& s;~PlainWipe(){if(!s.empty())SecureZeroMemory(s.data(),s.size());}}plainWipe{bytes};if(bytes.size()>16*1024)throw std::runtime_error("API key exceeds size limit");
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(entropyText)),reinterpret_cast<BYTE*>(const_cast<wchar_t*>(entropyText))},output{};
    if(!CryptProtectData(&input,L"OCR AI Translator API key",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw std::runtime_error("DPAPI encryption failed");
    struct Wipe { DATA_BLOB* b; ~Wipe(){if(b->pbData){SecureZeroMemory(b->pbData,b->cbData);LocalFree(b->pbData);}}} wipe{&output};
    unsigned char id[16]{};if(BCryptGenRandom(nullptr,id,sizeof(id),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("credential id generation failed");
    static constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring ref;for(auto b:id){ref+=hex[b>>4];ref+=hex[b&15];}
    const auto path=folder/(L"secret-"+ref+L".bin");atomicWrite(path,output.pbData,output.cbData);return ref;
}
std::wstring unprotect(const std::filesystem::path& folder,const std::wstring& ref){
    if(ref.empty())return {};if(ref.size()!=32||!std::all_of(ref.begin(),ref.end(),[](wchar_t c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::runtime_error("invalid credential reference");
    const auto bytes=readUtf8File(folder/(L"secret-"+ref+L".bin"));if(bytes.empty()||bytes.size()>64*1024)throw std::runtime_error("encrypted credential size is invalid");
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(entropyText)),reinterpret_cast<BYTE*>(const_cast<wchar_t*>(entropyText))},output{};
    if(!CryptUnprotectData(&input,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw std::runtime_error("DPAPI credential cannot be opened for this Windows user");
    struct Wipe { DATA_BLOB* b; ~Wipe(){if(b->pbData){SecureZeroMemory(b->pbData,b->cbData);LocalFree(b->pbData);}}} wipe{&output};
    std::string plain(reinterpret_cast<char*>(output.pbData),output.cbData);struct PlainWipe{std::string& s;~PlainWipe(){if(!s.empty())SecureZeroMemory(s.data(),s.size());}}plainWipe{plain};return json::decode(plain);
}
std::string serialize(const Config& c,const std::wstring& ref){
    auto root=c.unknownRoot;json::Value::Object ui;json::Value::Object provider;json::Value::Object translation;
    if(auto it=root.find("ui");it!=root.end()&&it->second.object())ui=*it->second.object();
    if(auto it=root.find("provider");it!=root.end()&&it->second.object())provider=*it->second.object();
    if(auto it=root.find("translation");it!=root.end()&&it->second.object())translation=*it->second.object();
    ui["target_language"]=u8(c.targetLanguage);ui["theme"]=c.theme==Theme::dark?"dark":(c.theme==Theme::light?"light":"system");
    ui["width_dip"]=static_cast<double>(c.widthDip);ui["height_dip"]=static_cast<double>(c.heightDip);ui["x_dip"]=static_cast<double>(c.xDip);ui["y_dip"]=static_cast<double>(c.yDip);
    ui["remember_position"]=c.rememberPosition;ui["always_on_top"]=c.alwaysOnTop;ui["show_source_copy"]=c.showSourceCopy;ui["show_unlock"]=c.showUnlock;ui["show_restore"]=c.showRestore;ui["show_result_copy"]=c.showResultCopy;ui["show_retranslate"]=c.showRetranslate;
    ui["show_rewrite"]=c.showRewrite;ui["show_de_ai"]=c.showDeAi;ui["show_custom_prompt"]=c.showCustomPrompt;ui["show_version_history"]=c.showVersionHistory;
    ui["version_limit"]=static_cast<double>(c.versionLimit);ui["remember_prompts"]=c.rememberPrompts;ui["confirm_ocr_candidates"]=c.confirmOcrCandidates;
    json::Value::Array toolbarOrder;
    for(const int id:c.toolbarOrder)toolbarOrder.emplace_back(static_cast<double>(id));
    ui["toolbar_order"]=std::move(toolbarOrder);
    provider["protocol"]="openai_chat_completions";provider["base_url"]=u8(c.baseUrl);provider["model"]=u8(c.model);provider["timeout_seconds"]=static_cast<double>(c.timeoutSeconds);provider["max_output_tokens"]=static_cast<double>(c.maxOutputTokens);provider["credential_ref"]=u8(ref);
    if(const auto* profile=c.findProfile(c.translationProfileId))provider["thinking_mode"]=thinkingModeName(profile->thinkingMode);
    translation["auto_start"]=c.autoStart;
    root["snipaste_exe_path"]=u8(c.snipasteExePath);

    json::Value::Array profilesArr;
    for(const auto& p:c.profiles){
        json::Value::Object po;
        po["id"]=u8(p.id);po["name"]=u8(p.name);po["base_url"]=u8(p.baseUrl);po["model"]=u8(p.model);
        po["timeout_seconds"]=static_cast<double>(p.timeoutSeconds);po["max_output_tokens"]=static_cast<double>(p.maxOutputTokens);po["thinking_mode"]=thinkingModeName(p.thinkingMode);
        po["credential_ref"]=u8(p.credentialRef);po["enabled"]=p.enabled;
        profilesArr.push_back(std::move(po));
    }
    json::Value::Object roles;
    roles["translation_profile_id"]=u8(c.translationProfileId);
    roles["rewrite_profile_id"]=u8(c.rewriteProfileId);

    root["schema_version"]=1.0;root["ui"]=std::move(ui);root["provider"]=std::move(provider);
    root["profiles"]=std::move(profilesArr);root["roles"]=std::move(roles);
    root["translation"]=std::move(translation);
    return json::stringify(root);
}
}
void clearSecret(std::wstring& s) noexcept {if(!s.empty())SecureZeroMemory(s.data(),s.size()*sizeof(wchar_t));s.clear();}
void clearConfigSecrets(Config& config) noexcept {clearSecret(config.apiKey);for(auto& p:config.profiles)clearSecret(p.apiKey);}

const ProviderProfile* Config::findProfile(const std::wstring& id) const noexcept {
    for(const auto& p:profiles)if(p.id==id)return &p;
    return nullptr;
}
ProviderProfile* Config::findProfile(const std::wstring& id) noexcept {
    for(auto& p:profiles)if(p.id==id)return &p;
    return nullptr;
}
const ProviderProfile& Config::getTranslationProfile() const noexcept {
    const auto* p=findProfile(translationProfileId);
    if(p)return *p;
    static const ProviderProfile defaultProfile{};
    return defaultProfile;
}
const ProviderProfile& Config::getRewriteProfile() const noexcept {
    const auto* p=findProfile(rewriteProfileId);
    if(p)return *p;
    static const ProviderProfile defaultProfile{};
    return defaultProfile;
}
void Config::syncActiveFromProfiles() noexcept {
    const auto* profile=findProfile(translationProfileId);
    if(!profile){
        activeSnapshotValid=false;
        activeSnapshotProfileId.clear();
        activeSnapshotBaseUrl.clear();
        activeSnapshotModel.clear();
        activeSnapshotCredentialRef.clear();
        activeSnapshotApiKeyFingerprint=0;
        activeSnapshotApiKeyLength=0;
        return;
    }
    const auto& p=*profile;
    baseUrl=p.baseUrl;model=p.model;apiKey=p.apiKey;credentialRef=p.credentialRef;timeoutSeconds=p.timeoutSeconds;maxOutputTokens=p.maxOutputTokens;reuseCredential=p.reuseCredential;
    activeSnapshotValid=true;
    activeSnapshotProfileId=translationProfileId;
    activeSnapshotBaseUrl=baseUrl;
    activeSnapshotModel=model;
    activeSnapshotCredentialRef=credentialRef;
    activeSnapshotTimeoutSeconds=timeoutSeconds;
    activeSnapshotMaxOutputTokens=maxOutputTokens;
    activeSnapshotReuseCredential=reuseCredential;
    activeSnapshotApiKeyFingerprint=apiKeyFingerprint(apiKey);
    activeSnapshotApiKeyLength=apiKey.size();
}
void Config::syncProfilesFromActive(){
    if(profiles.empty()){
        ProviderProfile p;p.id=L"default";p.name=L"默认模型";p.baseUrl=baseUrl;p.model=model;p.thinkingMode=defaultThinkingMode(p.baseUrl,p.model);p.apiKey=apiKey;p.credentialRef=credentialRef;p.timeoutSeconds=timeoutSeconds;p.maxOutputTokens=maxOutputTokens;p.reuseCredential=reuseCredential;
        profiles.push_back(std::move(p));
        translationProfileId=L"default";rewriteProfileId=L"default";
        syncActiveFromProfiles();
        return;
    }
    auto* p=findProfile(translationProfileId);
    if(!p)throw std::runtime_error("provider role references missing profile");
    const bool roleChanged=!activeSnapshotValid||activeSnapshotProfileId!=translationProfileId;
    const bool activeChanged=!activeSnapshotValid||
        activeSnapshotBaseUrl!=baseUrl||activeSnapshotModel!=model||
        activeSnapshotCredentialRef!=credentialRef||
        activeSnapshotTimeoutSeconds!=timeoutSeconds||
        activeSnapshotMaxOutputTokens!=maxOutputTokens||
        activeSnapshotReuseCredential!=reuseCredential||
        activeSnapshotApiKeyLength!=apiKey.size()||
        activeSnapshotApiKeyFingerprint!=apiKeyFingerprint(apiKey);
    // Role switches and profile-aware edits leave profiles authoritative.
    // The flat fields remain a compatibility path for legacy callers that
    // really changed the previously active profile.
    const bool applyLegacyFields=!roleChanged&&activeChanged;
    if(!applyLegacyFields)return;
    p->baseUrl=baseUrl;
    p->model=model;
    p->apiKey=apiKey;
    p->credentialRef=reuseCredential?credentialRef:L"";
    p->reuseCredential=reuseCredential&&!p->credentialRef.empty();
    p->timeoutSeconds=timeoutSeconds;
    p->maxOutputTokens=maxOutputTokens;
}

std::filesystem::path configFolder(bool create){
#if defined(SAT_ENABLE_TEST_HOOKS)
    wchar_t testMode[8]{};const DWORD testLen=GetEnvironmentVariableW(L"SAT_TEST_MODE",testMode,static_cast<DWORD>(std::size(testMode)));
    if(testLen==1&&testMode[0]==L'1'){
        wchar_t overridePath[32768]{};const DWORD n=GetEnvironmentVariableW(L"SAT_CONFIG_DIR",overridePath,static_cast<DWORD>(std::size(overridePath)));
        if(n>0&&n<std::size(overridePath)){std::filesystem::path p(overridePath);if(!p.is_absolute())throw std::runtime_error("test config path must be absolute");std::error_code ec;if(create){std::filesystem::create_directories(p,ec);if(ec)throw std::runtime_error("cannot create test config directory");}return p;}
    }
#endif
    PWSTR raw=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,create?KF_FLAG_CREATE:KF_FLAG_DEFAULT,nullptr,&raw)))throw std::runtime_error("Local AppData path unavailable");std::wstring path(raw);CoTaskMemFree(raw);path+=L"\\SnipasteAITranslator";if(create&&!CreateDirectoryW(path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("cannot create config directory");return path;
}
Config loadConfig(bool createFolder){
    Config c;c.folder=configFolder(createFolder);const auto path=c.folder/L"config.json";if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){if(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND){c.syncProfilesFromActive();return c;} c.warning=L"配置文件不可读；已使用安全默认值，原文件未修改。";c.damagedConfig=true;c.syncProfilesFromActive();return c;}
    try{
        Config parsed=c;struct WipeParsed{Config&c;~WipeParsed(){clearConfigSecrets(c);}}wipeParsed{parsed};auto raw=readUtf8File(path);struct RawWipe{std::string& text;~RawWipe(){if(!text.empty())SecureZeroMemory(text.data(),text.size());}}rawWipe{raw};auto root=json::Parser(raw).parse();struct RootWipe{json::Value& value;~RootWipe(){wipeJsonStrings(value);}}rootWipe{root};const auto*o=root.object();if(!o)throw std::runtime_error("config root is not an object");
        stripSensitive(root);parsed.unknownRoot=*root.object();parsed.schemaVersion=static_cast<uint32_t>(intField(root,"schema_version",1,1,1));
        parsed.snipasteExePath=field(root,"snipaste_exe_path",parsed.snipasteExePath,32700);
        if(const auto*translation=root.get("translation")){if(!translation->object())throw std::runtime_error("translation config must be an object");parsed.autoStart=boolField(*translation,"auto_start",true);}
        if(const auto*ui=root.get("ui")){if(!ui->object())throw std::runtime_error("ui config must be an object");
            parsed.targetLanguage=field(*ui,"target_language",L"zh-Hans",64);if(parsed.targetLanguage!=L"zh-Hans"&&parsed.targetLanguage!=L"zh-Hant"&&parsed.targetLanguage!=L"en"&&parsed.targetLanguage!=L"ja"&&parsed.targetLanguage!=L"ko"&&parsed.targetLanguage!=L"fr"&&parsed.targetLanguage!=L"de"&&parsed.targetLanguage!=L"es")throw std::runtime_error("unsupported target language");
            const auto theme=field(*ui,"theme",L"system",16);if(theme==L"dark")parsed.theme=Theme::dark;else if(theme==L"light")parsed.theme=Theme::light;else if(theme!=L"system")throw std::runtime_error("invalid theme");
            parsed.widthDip=intField(*ui,"width_dip",850,620,1600);parsed.heightDip=intField(*ui,"height_dip",380,360,1200);parsed.xDip=intField(*ui,"x_dip",0,-10000,10000);parsed.yDip=intField(*ui,"y_dip",0,-10000,10000);
            parsed.rememberPosition=boolField(*ui,"remember_position",false);parsed.alwaysOnTop=boolField(*ui,"always_on_top",false);parsed.showSourceCopy=boolField(*ui,"show_source_copy",true);parsed.showUnlock=boolField(*ui,"show_unlock",true);parsed.showRestore=boolField(*ui,"show_restore",true);parsed.showResultCopy=boolField(*ui,"show_result_copy",true);parsed.showRetranslate=boolField(*ui,"show_retranslate",true);
            parsed.showRewrite=boolField(*ui,"show_rewrite",true);parsed.showDeAi=boolField(*ui,"show_de_ai",true);parsed.showCustomPrompt=boolField(*ui,"show_custom_prompt",true);parsed.showVersionHistory=boolField(*ui,"show_version_history",true);
            parsed.versionLimit=static_cast<unsigned>(intField(*ui,"version_limit",10,static_cast<int>(minVersionLimit),static_cast<int>(maxVersionLimit)));
            parsed.rememberPrompts=boolField(*ui,"remember_prompts",true);parsed.confirmOcrCandidates=boolField(*ui,"confirm_ocr_candidates",false);
            parsed.toolbarOrder=toolbarOrderField(*ui,"toolbar_order",parsed.toolbarOrder);
        }
        if(const auto*profArrVal=root.get("profiles")){
            const auto*arr=profArrVal->array();
            if(!arr)throw std::runtime_error("profiles config must be an array");
            if(arr->empty())throw std::runtime_error("profiles config must not be empty");
            if(arr->size()>maxProfiles)throw std::runtime_error("too many provider profiles");
            for(const auto& item:*arr){
                if(!item.object())throw std::runtime_error("provider profile must be an object");
                if(!item.get("id"))throw std::runtime_error("provider profile id is required");
                ProviderProfile prof;
                prof.id=field(item,"id",L"default",maxProfileIdChars);
                prof.name=field(item,"name",L"模型配置",maxProfileNameChars);
                prof.baseUrl=field(item,"base_url",L"",2048);
                prof.model=field(item,"model",L"",maxModelChars);
                prof.thinkingMode=thinkingModeField(item,"thinking_mode",defaultThinkingMode(prof.baseUrl,prof.model));
                prof.thinkingModeExplicit=item.get("thinking_mode")!=nullptr;
                prof.timeoutSeconds=static_cast<unsigned>(intField(item,"timeout_seconds",60,5,180));
                prof.maxOutputTokens=static_cast<unsigned>(intField(item,"max_output_tokens",2048,1,32768));
                prof.enabled=boolField(item,"enabled",true);
                prof.credentialRef=field(item,"credential_ref",L"",32);
                if(!prof.credentialRef.empty()){
                    if(!isCredentialReference(prof.credentialRef))throw std::runtime_error("invalid credential reference");
                    prof.apiKey=unprotect(parsed.folder,prof.credentialRef);prof.reuseCredential=!prof.apiKey.empty();
                }
                parsed.profiles.push_back(std::move(prof));
            }
        }
        if(const auto*rolesVal=root.get("roles")){
            const auto*roles=rolesVal->object();
            if(!roles)throw std::runtime_error("roles config must be an object");
            parsed.translationProfileId=field(*roles,"translation_profile_id",L"default",maxProfileIdChars);
            parsed.rewriteProfileId=field(*roles,"rewrite_profile_id",L"default",maxProfileIdChars);
        }
        if(parsed.profiles.empty()&&root.get("provider")){
            const auto*p=root.get("provider");if(!p->object())throw std::runtime_error("provider config must be an object");if(field(*p,"protocol",L"openai_chat_completions",40)!=L"openai_chat_completions")throw std::runtime_error("unsupported API protocol");
            ProviderProfile prof;
            prof.id=L"default";prof.name=L"默认模型";
            prof.baseUrl=field(*p,"base_url",L"",2048);prof.model=field(*p,"model",L"",256);prof.thinkingMode=thinkingModeField(*p,"thinking_mode",defaultThinkingMode(prof.baseUrl,prof.model));prof.thinkingModeExplicit=p->get("thinking_mode")!=nullptr;prof.timeoutSeconds=static_cast<unsigned>(intField(*p,"timeout_seconds",60,5,180));prof.maxOutputTokens=static_cast<unsigned>(intField(*p,"max_output_tokens",2048,1,32768));prof.credentialRef=field(*p,"credential_ref",L"",32);if(!prof.credentialRef.empty()){prof.apiKey=unprotect(parsed.folder,prof.credentialRef);prof.reuseCredential=!prof.apiKey.empty();}
            if(!prof.credentialRef.empty()&&!isCredentialReference(prof.credentialRef))throw std::runtime_error("invalid credential reference");
            parsed.profiles.push_back(std::move(prof));
            parsed.translationProfileId=L"default";parsed.rewriteProfileId=L"default";
        }
        if(parsed.profiles.empty()){
            ProviderProfile prof;prof.id=L"default";prof.name=L"默认模型";
            parsed.profiles.push_back(std::move(prof));
            parsed.translationProfileId=L"default";parsed.rewriteProfileId=L"default";
        }
        validateConfigFields(parsed);
        validateProfileCollection(parsed);
        parsed.syncActiveFromProfiles();
        c=std::move(parsed);
    }catch(...){c.warning=L"配置损坏或凭据无法读取；原文件已保留，请先备份后再从设置修复。";c.damagedConfig=true;c.unknownRoot.clear();c.baseUrl.clear();c.model.clear();clearConfigSecrets(c);c.credentialRef.clear();c.profiles.clear();c.syncProfilesFromActive();}
    return c;
}
bool saveConfig(Config& candidate,std::wstring& error){
    error.clear();
    bool committed=false;
    std::vector<std::wstring> newlyCreatedRefs;
    std::vector<std::pair<std::wstring,bool>> originalProfileRefs;
    std::vector<std::pair<uint64_t,size_t>> originalProfileKeyMarkers;
    originalProfileRefs.reserve(candidate.profiles.size());
    originalProfileKeyMarkers.reserve(candidate.profiles.size());
    for(const auto& profile:candidate.profiles){
        originalProfileRefs.emplace_back(profile.credentialRef,profile.reuseCredential);
        originalProfileKeyMarkers.emplace_back(apiKeyFingerprint(profile.apiKey),profile.apiKey.size());
    }
    const std::wstring originalActiveRef=candidate.credentialRef;
    const bool originalActiveReuse=candidate.reuseCredential;
    const auto originalActiveKeyMarker=std::make_pair(apiKeyFingerprint(candidate.apiKey),candidate.apiKey.size());
    const auto cleanupUncommittedCredentials=[&]()noexcept{
        bool allClean=true;
        for(const auto& r:newlyCreatedRefs){
            if(!DeleteFileW((candidate.folder/(L"secret-"+r+L".bin")).c_str())){
                const DWORD err=GetLastError();
                if(err!=ERROR_FILE_NOT_FOUND&&err!=ERROR_PATH_NOT_FOUND)allClean=false;
            }
        }
        return allClean;
    };
    const auto restoreCredentialReferences=[&]()noexcept{
        const auto common=std::min(candidate.profiles.size(),originalProfileRefs.size());
        for(size_t i=0;i<common;++i){
            candidate.profiles[i].credentialRef=originalProfileRefs[i].first;
            const auto currentMarker=std::make_pair(apiKeyFingerprint(candidate.profiles[i].apiKey),candidate.profiles[i].apiKey.size());
            candidate.profiles[i].reuseCredential=currentMarker==originalProfileKeyMarkers[i]?originalProfileRefs[i].second:false;
        }
        for(size_t i=common;i<candidate.profiles.size();++i){candidate.profiles[i].credentialRef.clear();candidate.profiles[i].reuseCredential=false;}
        candidate.credentialRef=originalActiveRef;
        const auto currentActiveKeyMarker=std::make_pair(apiKeyFingerprint(candidate.apiKey),candidate.apiKey.size());
        candidate.reuseCredential=currentActiveKeyMarker==originalActiveKeyMarker?originalActiveReuse:false;
    };
    try{
        const auto folder=candidate.folder.empty()?configFolder():candidate.folder;candidate.folder=folder;
        candidate.syncProfilesFromActive();
        for(auto& profile:candidate.profiles)if(!profile.thinkingModeExplicit)profile.thinkingMode=defaultThinkingMode(profile.baseUrl,profile.model);
        validateConfigFields(candidate);
        validateProfileCollection(candidate);
        const auto configPath=folder/L"config.json";
        if(candidate.damagedConfig&&GetFileAttributesW(configPath.c_str())!=INVALID_FILE_ATTRIBUTES){
            SYSTEMTIME now{};GetSystemTime(&now);wchar_t suffix[64]{};swprintf_s(suffix,L".damaged-%04u%02u%02u-%02u%02u%02u-",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
            unsigned char random[8]{};if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("corrupt config backup failed");std::wstring name=L"config.json"+std::wstring(suffix);constexpr wchar_t hex[]=L"0123456789abcdef";for(auto byte:random){name+=hex[byte>>4];name+=hex[byte&15];}name+=L".bak";
            if(!CopyFileW(configPath.c_str(),(folder/name).c_str(),TRUE))throw std::runtime_error("corrupt config backup failed");const auto recovery=folder/(name+L".credentials");if(!CreateDirectoryW(recovery.c_str(),nullptr))throw std::runtime_error("corrupt credential backup failed");
            for(const auto& entry:std::filesystem::directory_iterator(folder)){const auto file=entry.path().filename().wstring();if(!entry.is_regular_file()||file.size()!=43||file.compare(0,7,L"secret-")!=0||file.compare(39,4,L".bin")!=0)continue;const auto ref=file.substr(7,32);if(!std::all_of(ref.begin(),ref.end(),[](wchar_t ch){return(ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f');}))continue;if(!CopyFileW(entry.path().c_str(),(recovery/file).c_str(),TRUE))throw std::runtime_error("corrupt credential backup failed");}
        }
        for(auto& p:candidate.profiles){
            if(p.reuseCredential&&!p.apiKey.empty()&&p.credentialRef.empty())throw std::runtime_error("credential reference is required for reuse");
            if(p.reuseCredential&&!p.apiKey.empty()&&!p.credentialRef.empty()){
                std::wstring verified=unprotect(folder,p.credentialRef);
                const bool matches=verified==p.apiKey;
                clearSecret(verified);
                if(!matches)throw std::runtime_error("credential reference does not match profile");
            }else if(!p.apiKey.empty()){
                std::wstring ref=protect(p.apiKey,folder);
                newlyCreatedRefs.push_back(ref);
                p.credentialRef=std::move(ref);
                p.reuseCredential=true;
            }else{
                p.credentialRef.clear();
                p.reuseCredential=false;
            }
        }
        candidate.syncActiveFromProfiles();
        std::vector<std::wstring> keepRefs;
        for(const auto& p:candidate.profiles)if(!p.credentialRef.empty())keepRefs.push_back(p.credentialRef);
        if(candidate.profiles.empty()&&!candidate.credentialRef.empty())keepRefs.push_back(candidate.credentialRef);
        const auto data=serialize(candidate,candidate.credentialRef);
        atomicWrite(configPath,data.data(),data.size());committed=true;for(auto& profile:candidate.profiles)profile.thinkingModeExplicit=true;

        candidate.warning.clear();
        if(!cleanupOrphanCredentials(folder,keepRefs))candidate.warning=L"设置已保存，但旧的加密 API 凭据未能清理；请检查配置目录权限后再次保存。";
        candidate.damagedConfig=false;
        return true;
    }catch(const std::exception& failure){
        if(committed)return true; // Never roll back credentials referenced by a committed config.
        const bool cleaned=cleanupUncommittedCredentials();
        restoreCredentialReferences();
        if(error.empty()){const std::string_view reason=failure.what();if(reason=="corrupt config backup failed"||reason=="corrupt credential backup failed")error=L"损坏配置及加密凭据的备份未完成；原设置保持不变。请检查配置目录权限和磁盘空间。";else if(reason=="atomic config replace failed")error=L"配置文件被占用或无法替换；原设置保持不变。";else if(reason=="cannot create temporary config file")error=L"无法创建临时配置文件，请检查配置目录权限和磁盘空间。";else if(reason=="DPAPI encryption failed"||reason=="credential id generation failed")error=L"Windows 凭据保护失败；API Key 未保存。";else if(reason=="config write failed"||reason=="config flush failed")error=L"配置文件写入失败；原设置保持不变。";else error=L"设置未保存。请检查配置目录权限、Base URL、模型名与 API Key。";}
        if(!cleaned){if(!error.empty())error+=L" ";error+=L"未使用的加密 API 凭据文件也无法清理。";}return false;
    }catch(...){
        if(committed)return true;
        const bool cleaned=cleanupUncommittedCredentials();
        restoreCredentialReferences();
        if(error.empty())error=L"设置未保存。请检查配置目录权限和字段格式。";
        if(!cleaned){if(!error.empty())error+=L" ";error+=L"未使用的加密 API 凭据文件也无法清理。";}return false;
    }
}
bool validateEndpoint(const std::wstring& baseUrl,std::wstring& error){
    if(baseUrl.empty()||baseUrl.size()>2048){error=L"请输入有效的 Base URL。";return false;}
    URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwSchemeLength=static_cast<DWORD>(-1);u.dwHostNameLength=static_cast<DWORD>(-1);u.dwUserNameLength=static_cast<DWORD>(-1);u.dwPasswordLength=static_cast<DWORD>(-1);u.dwUrlPathLength=static_cast<DWORD>(-1);u.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(baseUrl.c_str(),static_cast<DWORD>(baseUrl.size()),0,&u)){error=L"Base URL 格式无效。";return false;}
    if(!u.lpszHostName||u.dwHostNameLength==0){error=L"Base URL 必须包含主机名。";return false;}
    if(u.dwUserNameLength||u.dwPasswordLength||u.dwExtraInfoLength){error=L"Base URL 不得包含账号、密码、查询参数或片段。";return false;}
    if(u.nScheme!=INTERNET_SCHEME_HTTPS&&u.nScheme!=INTERNET_SCHEME_HTTP){error=L"仅支持 HTTPS；HTTP 仅允许回环本地服务。";return false;}
    if(u.nScheme==INTERNET_SCHEME_HTTP){std::wstring host(u.lpszHostName,u.dwHostNameLength);std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});if(host!=L"localhost"&&host!=L"127.0.0.1"&&host!=L"::1"){error=L"出于安全考虑，HTTP 只允许 localhost/127.0.0.1。";return false;}}
    return true;
}
}
