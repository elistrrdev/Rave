#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace sys
{
    bool isElevated();

    DWORD windowsBuild();
    bool isWindows11();
    std::string windowsName();

    std::wstring exePath();
    void openUrl(const std::wstring& url);
    bool launchUnelevated(const std::wstring& exe, const std::wstring& args, const std::wstring& dir);
    std::vector<DWORD> findProcesses(const std::wstring& exeName);

    std::string narrow(const std::wstring& s);
    std::wstring widen(const std::string& s);
    std::string md5Hex(const void* data, size_t len);
    std::string sha256Hex(const void* data, size_t len);
}
