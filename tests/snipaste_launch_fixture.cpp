#include <windows.h>
#include "json.hpp"
#include <filesystem>
#include <fstream>

// Test-only stand-in: records argv/cwd, then stays alive until the test releases it.
// It never captures the desktop or touches the clipboard.
int wmain(int argc,wchar_t** argv){
    try{
        const auto eventName=L"Local\\SAT.SnipasteFixture."+std::to_wstring(GetCurrentProcessId());
        HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());if(!stop)return 2;
        sat::json::Value::Array args;for(int i=0;i<argc;++i)args.emplace_back(sat::json::encode(argv[i]));
        sat::json::Value::Object report;report["pid"]=static_cast<double>(GetCurrentProcessId());report["args"]=std::move(args);report["cwd"]=sat::json::encode(std::filesystem::current_path().wstring());
        {std::ofstream out("launch.json",std::ios::binary|std::ios::trunc);out<<sat::json::stringify(report);if(!out){CloseHandle(stop);return 3;}}
        const DWORD result=WaitForSingleObject(stop,10000);CloseHandle(stop);return result==WAIT_OBJECT_0?0:4;
    }catch(...){return 5;}
}
