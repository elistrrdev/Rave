#include "net.hpp"
#include "../version.hpp"
#include <winhttp.h>
#include <netlistmgr.h>
#include <wrl/client.h>
#include <cctype>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace
{
    HINTERNET session()
    {
        static HINTERNET handle = nullptr;
        static std::once_flag once;
        std::call_once(once, [] {
            const wchar_t* agent = RAVE_WNAME L"/" RAVE_VERSION_WSTR;
            handle = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!handle)
                handle = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (handle)
            {
                DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
                if (!WinHttpSetOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
                {
                    protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                    WinHttpSetOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
                }
            }
        });
        return handle;
    }

    struct Handle
    {
        HINTERNET h = nullptr;
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };

    bool startsWithNoCase(const std::string& text, size_t at, const std::string& prefix)
    {
        if (text.size() < at + prefix.size())
            return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (tolower((unsigned char)text[at + i]) != tolower((unsigned char)prefix[i]))
                return false;
        return true;
    }

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos)
            return {};
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }

    template<typename Fn>
    void eachHeader(const std::string& raw, const std::string& name, Fn fn)
    {
        size_t pos = 0;
        while (pos < raw.size())
        {
            size_t eol = raw.find("\r\n", pos);
            if (eol == std::string::npos)
                eol = raw.size();
            size_t colon = pos + name.size();
            if (colon < eol && raw[colon] == ':' && startsWithNoCase(raw, pos, name))
            {
                if (!fn(trim(raw.substr(colon + 1, eol - colon - 1))))
                    return;
            }
            pos = eol + 2;
        }
    }
}

std::string net::Response::header(const std::string& name) const
{
    std::string value;
    eachHeader(headers, name, [&](const std::string& v) { value = v; return false; });
    return value;
}

std::string net::Response::cookie(const std::string& name) const
{
    std::string value;
    eachHeader(headers, "set-cookie", [&](const std::string& v) {
        size_t eq = v.find('=');
        if (eq == std::string::npos || trim(v.substr(0, eq)) != name)
            return true;
        size_t semi = v.find(';', eq + 1);
        value = v.substr(eq + 1, semi == std::string::npos ? std::string::npos : semi - eq - 1);
        return false;
    });
    return value;
}

bool net::send(const Request& req, Response& out, const Progress& progress)
{
    out = {};
    HINTERNET s = session();
    if (!s)
        return false;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(req.url.c_str(), (DWORD)req.url.size(), 0, &uc))
        return false;

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo)
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty())
        path = L"/";

    Handle connect{ WinHttpConnect(s, host.c_str(), uc.nPort, 0) };
    if (!connect.h)
        return false;

    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{ WinHttpOpenRequest(connect.h, req.method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
    if (!request.h)
        return false;

    int timeout = (int)req.timeoutMs;
    WinHttpSetTimeouts(request.h, timeout, timeout, timeout, timeout * 2);

    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    DWORD noCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));

    const wchar_t* hdr = req.headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : req.headers.c_str();
    DWORD hdrLen = req.headers.empty() ? 0 : (DWORD)-1L;
    LPVOID data = req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data();
    DWORD dataLen = (DWORD)req.body.size();
    if (!WinHttpSendRequest(request.h, hdr, hdrLen, data, dataLen, dataLen, 0))
        return false;
    if (!WinHttpReceiveResponse(request.h, nullptr))
        return false;

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return false;
    out.status = (int)status;

    DWORD rawSize = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (rawSize > 0)
    {
        std::wstring raw(rawSize / sizeof(wchar_t) + 1, L' ');
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX))
        {
            raw.resize(rawSize / sizeof(wchar_t));
            out.headers.reserve(raw.size());
            for (wchar_t c : raw)
                out.headers += (char)(c < 0x80 ? c : '?');
        }
    }

    uint64_t total = 0;
    wchar_t lenBuf[32]{};
    DWORD lenSize = sizeof(lenBuf);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(lenBuf, nullptr, 10);
    if (total > 0 && total < (1ull << 31))
        out.body.reserve((size_t)total);

    std::string chunk;
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail))
            return false;
        if (avail == 0)
            break;
        chunk.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), avail, &read))
            return false;
        if (read == 0)
            break;
        out.body.append(chunk.data(), read);
        if (progress && !progress(out.body.size(), total))
            return false;
    }
    return true;
}

bool net::get(const std::wstring& url, Response& out, const Progress& progress, const std::wstring& headers, DWORD timeoutMs)
{
    Request req;
    req.url = url;
    req.headers = headers;
    req.timeoutMs = timeoutMs;
    return send(req, out, progress);
}

bool net::hasInternet()
{
    ComPtr<INetworkListManager> nlm;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&nlm))))
    {
        NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
        if (SUCCEEDED(nlm->GetConnectivity(&connectivity)) && connectivity == NLM_CONNECTIVITY_DISCONNECTED)
            return false;
    }

    Response r;
    if (get(L"http://www.msftconnecttest.com/connecttest.txt", r, nullptr, L"", 5000) && r.status == 200 && r.body.rfind("Microsoft Connect Test", 0) == 0)
        return true;

    if (get(L"https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer", r, nullptr, L"", 5000) && r.ok())
        return true;

    return false;
}
