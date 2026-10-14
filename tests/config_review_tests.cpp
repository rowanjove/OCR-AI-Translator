#include "config_store.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void requireReview(bool condition,const char* message){
    if(!condition)throw std::runtime_error(message);
}
sat::ProviderProfile reviewProfile(const wchar_t* id,const wchar_t* url,const wchar_t* model){
    sat::ProviderProfile profile;
    profile.id=id;
    profile.name=id;
    profile.baseUrl=url;
    profile.model=model;
    return profile;
}
class ReviewConfigDirectory {
public:
    ReviewConfigDirectory(){
        std::wstring mode(8,L'\0'),path(32768,L'\0');
        const DWORD modeSize=GetEnvironmentVariableW(L"SAT_TEST_MODE",mode.data(),static_cast<DWORD>(mode.size()));
        const DWORD pathSize=GetEnvironmentVariableW(L"SAT_CONFIG_DIR",path.data(),static_cast<DWORD>(path.size()));
        hadMode_=modeSize>0;hadPath_=pathSize>0;
        if(hadMode_)oldMode_.assign(mode.data(),modeSize);
        if(hadPath_)oldPath_.assign(path.data(),pathSize);
        directory_=std::filesystem::temp_directory_path()/(L"fanyi-config-review-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(directory_);
        SetEnvironmentVariableW(L"SAT_TEST_MODE",L"1");
        SetEnvironmentVariableW(L"SAT_CONFIG_DIR",directory_.c_str());
    }
    ~ReviewConfigDirectory(){
        SetEnvironmentVariableW(L"SAT_TEST_MODE",hadMode_?oldMode_.c_str():nullptr);
        SetEnvironmentVariableW(L"SAT_CONFIG_DIR",hadPath_?oldPath_.c_str():nullptr);
        std::error_code error;std::filesystem::remove_all(directory_,error);
    }
    void write(std::string_view text)const{
        std::ofstream output(directory_/L"config.json",std::ios::binary|std::ios::trunc);
        output<<text;
        if(!output)throw std::runtime_error("could not write config review fixture");
    }
private:
    std::filesystem::path directory_;
    bool hadMode_=false,hadPath_=false;
    std::wstring oldMode_,oldPath_;
};
void requireDamaged(ReviewConfigDirectory& directory,std::string_view json,const char* message){
    directory.write(json);
    requireReview(sat::loadConfig().damagedConfig,message);
}
}

// This entrypoint is intentionally main-less so the review slice can be
// linked into an existing test runner without changing the product build.
void runConfigReviewTests(){
    {
        sat::Config config;
        config.profiles={reviewProfile(L"translate",L"https://translate.example/v1",L"translate-model"),reviewProfile(L"rewrite",L"https://rewrite.example/v1",L"rewrite-model")};
        config.translationProfileId=L"translate";
        config.rewriteProfileId=L"rewrite";
        config.syncActiveFromProfiles();

        // A role switch must keep the selected profile authoritative even if
        // the legacy flat fields still describe the former active profile.
        config.translationProfileId=L"rewrite";
        config.baseUrl=L"https://translate.example/v1";
        config.model=L"translate-model";
        config.syncProfilesFromActive();
        const auto* rewrite=config.findProfile(L"rewrite");
        requireReview(rewrite&&rewrite->baseUrl==L"https://rewrite.example/v1"&&rewrite->model==L"rewrite-model","role switch overwrote the selected profile");
    }
    {
        sat::Config config;
        config.profiles={reviewProfile(L"default",L"https://old.example/v1",L"old-model")};
        config.translationProfileId=L"default";
        config.rewriteProfileId=L"default";
        config.syncActiveFromProfiles();
        config.baseUrl=L"https://new.example/v1";
        config.model=L"new-model";
        config.syncProfilesFromActive();
        const auto* profile=config.findProfile(L"default");
        requireReview(profile&&profile->baseUrl==L"https://new.example/v1"&&profile->model==L"new-model","legacy active field edit was not migrated");
    }
    {
        sat::Config config;
        config.profiles={reviewProfile(L"enabled",L"https://enabled.example/v1",L"enabled-model"),reviewProfile(L"disabled",L"https://disabled.example/v1",L"disabled-model")};
        config.translationProfileId=L"enabled";
        config.rewriteProfileId=L"disabled";
        config.profiles[1].enabled=false;
        requireReview(config.getProfileForRole(L"translate")!=nullptr,"enabled role profile was not returned");
        requireReview(config.getProfileForRole(L"rewrite")==nullptr,"disabled role profile was returned");
        requireReview(config.getProfileForRole(L"missing")==nullptr,"missing role profile silently fell back");
    }
    {
        sat::Config config;
        config.profiles={reviewProfile(L"duplicate",L"https://one.example/v1",L"one"),reviewProfile(L"duplicate",L"https://two.example/v1",L"two")};
        config.translationProfileId=L"duplicate";
        config.rewriteProfileId=L"duplicate";
        config.folder=L"config-review-duplicate";
        std::wstring error;
        requireReview(!sat::saveConfig(config,error),"duplicate profile IDs were accepted");
    }
    {
        sat::Config config;
        config.profiles={reviewProfile(L"missing-role",L"https://one.example/v1",L"one")};
        config.translationProfileId=L"missing-role";
        config.rewriteProfileId=L"missing-role";
        config.profiles.front().baseUrl=L"https://one.example/v1?secret=query";
        config.folder=L"config-review-endpoint";
        std::wstring error;
        requireReview(!sat::saveConfig(config,error),"invalid profile endpoint was accepted");
    }
    {
        sat::Config config;
        config.translationProfileId=L"default";
        config.rewriteProfileId=L"default";
        for(unsigned i=0;i<33;++i)config.profiles.push_back(reviewProfile((L"p"+std::to_wstring(i)).c_str(),L"https://example.test/v1",L"model"));
        config.folder=L"config-review-count";
        std::wstring error;
        requireReview(!sat::saveConfig(config,error),"unbounded profile count was accepted");
    }
    {
        sat::Config config;
        config.profiles={reviewProfile(L"default",L"https://example.test/v1",L"model")};
        config.folder=L"config-review-ui-fields";
        config.versionLimit=51;
        std::wstring error;
        requireReview(!sat::saveConfig(config,error),"version limit above the bound was accepted");
        config.versionLimit=10;
        config.toolbarOrder[0]=config.toolbarOrder[1];
        requireReview(!sat::saveConfig(config,error),"duplicate toolbar action was accepted");
    }
    {
        ReviewConfigDirectory directory;
        requireDamaged(directory,R"({"profiles":{}})","non-array profiles were accepted");
        requireDamaged(directory,R"({"profiles":[]})","empty modern profiles silently used a legacy fallback");
        requireDamaged(directory,R"({"roles":[]})","non-object roles were accepted");
        requireDamaged(directory,R"({"profiles":[1]})","non-object provider profile was accepted");
        requireDamaged(directory,R"({"profiles":[{"id":""}]})","empty provider profile ID was accepted");
        requireDamaged(directory,R"({"profiles":[{"id":"a"},{"id":"a"}]})","duplicate provider profile ID was accepted");
        requireDamaged(directory,R"({"profiles":[{"id":"a"}],"roles":{"translation_profile_id":"missing","rewrite_profile_id":"a"}})","missing role reference was accepted");
        requireDamaged(directory,R"({"ui":{"version_limit":51}})","version limit above the bound was accepted on load");
        requireDamaged(directory,R"({"ui":{"toolbar_order":[117]}})","incomplete toolbar order was accepted on load");
    }
    {
        ReviewConfigDirectory directory;sat::Config config=sat::loadConfig();std::wstring error;
        config.baseUrl=L"https://example.test/v1";config.model=L"model";config.apiKey=L"old-review-key";
        requireReview(sat::saveConfig(config,error),"transaction seed save failed");
        const auto originalRef=config.credentialRef;config.apiKey=L"rotated-review-key";config.reuseCredential=false;
        const auto path=config.folder/L"config.json";HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        requireReview(lock!=INVALID_HANDLE_VALUE,"transaction fixture lock failed");
        const bool saved=sat::saveConfig(config,error);CloseHandle(lock);
        requireReview(!saved,"locked transaction unexpectedly committed");
        requireReview(config.profiles[0].credentialRef==originalRef&&!config.profiles[0].reuseCredential,"failed rotation retained a deleted new credential or stale reuse flag");
        requireReview(sat::saveConfig(config,error),"retrying same failed transaction object failed");
        auto loaded=sat::loadConfig();requireReview(loaded.apiKey==L"rotated-review-key","retry saved an unusable credential");
        sat::clearConfigSecrets(loaded);sat::clearConfigSecrets(config);
    }
}
