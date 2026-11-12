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
        std::string headers;
        bool ok() const { return status >= 200 && status < 300; }
        std::string header(const std::string& name) const;
        std::string cookie(const std::string& name) const;
    };

    struct Request
    {
        std::wstring method = L"GET";
        std::wstring url;
        std::wstring headers;
        std::string body;
        DWORD timeoutMs = 15000;
    };

    using Progress = std::function<bool(uint64_t done, uint64_t total)>;

    bool send(const Request& request, Response& out, const Progress& progress = nullptr);
    bool get(const std::wstring& url, Response& out, const Progress& progress = nullptr, const std::wstring& headers = L"", DWORD timeoutMs = 15000);
    bool hasInternet();
}
