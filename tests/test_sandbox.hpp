#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <cwchar>

inline std::filesystem::path requireTestDirectory(const wchar_t* leaf) {
    wchar_t mode[8]{},directory[32768]{},executable[32768]{};
    if(GetEnvironmentVariableW(L"SAT_TEST_MODE",mode,8)!=1||mode[0]!=L'1')
        throw std::runtime_error("refusing destructive tests without SAT_TEST_MODE=1");
    const DWORD size=GetEnvironmentVariableW(L"SAT_CONFIG_DIR",directory,32768);
    const DWORD executableSize=GetModuleFileNameW(nullptr,executable,32768);
    if(!size||size>=32768||!executableSize||executableSize>=32768)
        throw std::runtime_error("isolated test directory is unavailable");
    const auto requested=std::filesystem::path(directory).lexically_normal();
    const auto expected=(std::filesystem::path(executable).parent_path().parent_path()/leaf).lexically_normal();
    if(!requested.is_absolute()||_wcsicmp(requested.c_str(),expected.c_str())!=0)
        throw std::runtime_error("refusing tests outside the build's dedicated test directory");
    const DWORD attributes=GetFileAttributesW(requested.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("refusing a reparse-point test directory");
    return requested;
}
