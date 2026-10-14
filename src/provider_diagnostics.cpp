#include "provider_diagnostics.hpp"
#include "translation_client.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>

namespace sat {
namespace {
const char* phaseName(TranslationPhase phase){switch(phase){case TranslationPhase::send:return "send";case TranslationPhase::headers:return "headers";case TranslationPhase::body:return "body";case TranslationPhase::complete:return "complete";default:return "setup";}}
}
json::Value providerDiagnostic(const Config& config,bool online,bool rewrite,bool allowLoopbackForTests){
    json::Value::Object report;
    report["schema_version"]=1.0;report["mode"]=online?"online_single_request":"offline";
    report["role"]=rewrite?"rewrite":"translation";report["config_readable"]=!config.damagedConfig&&config.warning.empty();
    report["network_requests"]=0.0;report["http_status"]=0.0;report["winhttp_error"]=0.0;
    report["live_provider_verified"]=false;report["result_received"]=false;report["response_is_sse"]=false;
    report["phase"]="not_started";report["status"]="UNVERIFIED";
    report["request_cap"]=1.0;report["output_token_cap"]=64.0;report["stream_fallback_blocked"]=false;
    const auto* profile=config.getProfileForRole(rewrite?L"rewrite":L"translate");
    const bool configured=profile&&!profile->baseUrl.empty()&&!profile->model.empty()&&!profile->apiKey.empty();
    std::wstring endpointError;const bool endpointValid=profile&&!profile->baseUrl.empty()&&validateEndpoint(profile->baseUrl,endpointError);
    std::wstring scheme=profile?profile->baseUrl.substr(0,8):L"";for(auto& c:scheme)c=static_cast<wchar_t>(towlower(c));
    const bool https=scheme==L"https://";
    report["profile_enabled"]=profile!=nullptr;report["configured"]=configured;
    report["endpoint_valid"]=endpointValid;report["https"]=https;
    if(config.damagedConfig||!config.warning.empty()){report["reason"]="config_unreadable_or_credentials_unavailable";return report;}
    if(!configured){report["reason"]="configure_enabled_profile_endpoint_model_and_dpapi_key";return report;}
    if(!endpointValid){report["reason"]="invalid_endpoint";return report;}
    if(!https&&!allowLoopbackForTests){report["reason"]="https_required_for_release_verification";return report;}
    if(!online){report["reason"]="network_not_requested";return report;}

    TranslationRequest request;struct RequestWiper{TranslationRequest& r;~RequestWiper(){clearSecret(r.apiKey);}}requestWiper{request};request.baseUrl=profile->baseUrl;request.model=profile->model;request.apiKey=profile->apiKey;
    request.text=L"Hello";request.targetLanguage=config.targetLanguage;request.modelProfileId=profile->id;request.thinkingMode=profile->thinkingMode;
    request.timeoutSeconds=std::min(30u,profile->timeoutSeconds);request.maxOutputTokens=64;request.maxNetworkRequests=1;request.testConnection=true;
    const auto begin=std::chrono::steady_clock::now();std::mutex mutex;std::condition_variable changed;
    bool finished=false,terminal=false,received=false;TranslationEvent last{TranslationEvent::Kind::error};
    try{
        auto task=TranslationTask::start(std::move(request),[&](TranslationEvent event){
            std::lock_guard lock(mutex);if(event.kind==TranslationEvent::Kind::delta)received=received||!event.text.empty();
            else{terminal=true;last.kind=event.kind;last.httpStatus=event.httpStatus;last.networkError=event.networkError;last.networkRequests=event.networkRequests;last.responseIsStream=event.responseIsStream;last.streamFallbackBlocked=event.streamFallbackBlocked;last.phase=event.phase;}
            // Never retain the model/error text in diagnostics.
            clearSecret(event.text);
        },[&]{std::lock_guard lock(mutex);finished=true;changed.notify_all();});
        {std::unique_lock lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(std::min(30u,profile->timeoutSeconds)+10),[&]{return finished;}))task->cancel();}
        task->join();
    }catch(...){report["status"]="FAILED";report["reason"]="request_setup_failed";return report;}
    report["elapsed_ms"]=static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count());
    report["network_requests"]=static_cast<double>(last.networkRequests);report["http_status"]=static_cast<double>(last.httpStatus);report["winhttp_error"]=static_cast<double>(last.networkError);
    report["phase"]=phaseName(last.phase);report["response_is_sse"]=last.responseIsStream;report["stream_fallback_blocked"]=last.streamFallbackBlocked;
    const bool success=terminal&&last.kind==TranslationEvent::Kind::complete&&received;
    report["status"]=success?"PASS":"FAILED";report["live_provider_verified"]=success&&https&&!allowLoopbackForTests;report["result_received"]=success;
    report["reason"]=success?"complete_text_received":last.streamFallbackBlocked?"stream_unsupported_no_second_request":last.networkError?"winhttp_network_or_tls_failure":last.httpStatus>=400?"http_rejected":last.kind==TranslationEvent::Kind::cancelled?"cancelled":last.phase==TranslationPhase::setup?"request_setup_failed":"incomplete_or_invalid_response";
    return report;
}
int runProviderDiagnostic(bool online,bool rewrite,bool allowLoopbackForTests){
    json::Value report;
    try{Config config=loadConfig(false);struct Wiper{Config& c;~Wiper(){clearConfigSecrets(c);}}wiper{config};report=providerDiagnostic(config,online,rewrite,allowLoopbackForTests);}
    catch(...){json::Value::Object error;error["status"]="UNVERIFIED";error["reason"]="config_unavailable";error["network_requests"]=0.0;report=std::move(error);}
    const auto output=json::stringify(report)+"\n";const HANDLE handle=GetStdHandle(STD_OUTPUT_HANDLE);size_t position=0;
    while(position<output.size()){DWORD written=0;const DWORD count=static_cast<DWORD>(std::min<size_t>(output.size()-position,64*1024));if(!WriteFile(handle,output.data()+position,count,&written,nullptr)||!written)return 3;position+=written;}
    const auto* value=report.get("status");const auto* status=value?value->string():nullptr;
    return status&&(*status=="PASS"||(!online&&*status=="UNVERIFIED"))?0:2;
}
}
