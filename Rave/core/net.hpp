#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>

namespace net
{
    struct Response
    {
        int status = 0;
        std::string body;
        bool ok() const { return status >= 200 && status < 300; }
    };

    using Progress = std::function<bool(uint64_t done, uint64_t total)>;

    bool get(const std::wstring& url, Response& out, const Progress& progress = nullptr, const std::wstring& headers = L"", DWORD timeoutMs = 15000);
    bool hasInternet();
}
