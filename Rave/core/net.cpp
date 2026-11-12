#include "net.hpp"
#include "../version.hpp"
#include <winhttp.h>
#include <netlistmgr.h>
#include <wrl/client.h>
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
}

bool net::get(const std::wstring& url, Response& out, const Progress& progress, const std::wstring& headers, DWORD timeoutMs)
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
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc))
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
    Handle request{ WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
    if (!request.h)
        return false;

    WinHttpSetTimeouts(request.h, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs * 2);

    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    const wchar_t* hdr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    DWORD hdrLen = headers.empty() ? 0 : (DWORD)-1L;
    if (!WinHttpSendRequest(request.h, hdr, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return false;
    if (!WinHttpReceiveResponse(request.h, nullptr))
        return false;

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return false;
    out.status = (int)status;

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
