#include "system.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

bool sys::isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

sys::Elevation sys::relaunchElevated(HWND owner)
{
    std::wstring exe = exePath();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei))
        return Elevation::Started;
    return GetLastError() == ERROR_CANCELLED ? Elevation::Cancelled : Elevation::Failed;
}

DWORD sys::windowsBuild()
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    static DWORD build = [] {
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto fn = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
        if (fn && fn(&info) == 0)
            return info.dwBuildNumber;
        return (DWORD)0;
    }();
    return build;
}

bool sys::isWindows11()
{
    return windowsBuild() >= 22000;
}

std::string sys::windowsName()
{
    auto read = [](const wchar_t* value) {
        wchar_t buf[256]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
            return std::wstring(buf);
        return std::wstring();
    };

    std::wstring product = read(L"ProductName");
    if (product.empty())
        product = L"Windows";
    if (isWindows11())
    {
        size_t at = product.find(L"Windows 10");
        if (at != std::wstring::npos)
            product.replace(at, 10, L"Windows 11");
    }
    std::wstring display = read(L"DisplayVersion");
    std::wstring result = product;
    if (!display.empty())
        result += L" " + display;
    result += L" (" + std::to_wstring(windowsBuild()) + L")";
    return narrow(result);
}

std::wstring sys::exePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
        if (n < path.size())
        {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

namespace
{
    bool shellExecuteAsDesktop(const std::wstring& exe, const std::wstring& args, const std::wstring& dir)
    {
        ComPtr<IShellWindows> windows;
        if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows))))
            return false;

        VARIANT empty{};
        long hwnd = 0;
        ComPtr<IDispatch> dispatch;
        if (windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &dispatch) != S_OK || !dispatch)
            return false;

        ComPtr<IServiceProvider> provider;
        if (FAILED(dispatch.As(&provider)))
            return false;
        ComPtr<IShellBrowser> browser;
        if (FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))))
            return false;
        ComPtr<IShellView> view;
        if (FAILED(browser->QueryActiveShellView(&view)))
            return false;
        ComPtr<IDispatch> background;
        if (FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))))
            return false;
        ComPtr<IShellFolderViewDual> folderView;
        if (FAILED(background.As(&folderView)))
            return false;
        ComPtr<IDispatch> application;
        if (FAILED(folderView->get_Application(&application)))
            return false;
        ComPtr<IShellDispatch2> shell;
        if (FAILED(application.As(&shell)))
            return false;

        BSTR file = SysAllocString(exe.c_str());
        VARIANT vArgs{}, vDir{}, vOp{}, vShow{};
        vArgs.vt = VT_BSTR;
        vArgs.bstrVal = SysAllocString(args.c_str());
        vDir.vt = VT_BSTR;
        vDir.bstrVal = SysAllocString(dir.c_str());
        vOp.vt = VT_BSTR;
        vOp.bstrVal = SysAllocString(L"open");
        vShow.vt = VT_I4;
        vShow.lVal = SW_SHOWNORMAL;

        HRESULT hr = shell->ShellExecute(file, vArgs, vDir, vOp, vShow);

        SysFreeString(file);
        VariantClear(&vArgs);
        VariantClear(&vDir);
        VariantClear(&vOp);
        return SUCCEEDED(hr);
    }
}

bool sys::launchUnelevated(const std::wstring& exe, const std::wstring& args, const std::wstring& dir)
{
    if (isElevated() && shellExecuteAsDesktop(exe, args, dir))
        return true;

    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::vector<DWORD> sys::findProcesses(const std::wstring& exeName)
{
    std::vector<DWORD> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0)
                result.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

void sys::openFolder(const std::wstring& path)
{
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string sys::narrow(const std::wstring& s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring sys::widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string sys::md5Hex(const void* data, size_t len)
{
    unsigned char digest[16]{};
    if (BCryptHash(BCRYPT_MD5_ALG_HANDLE, nullptr, 0, (PUCHAR)data, (ULONG)len, digest, sizeof(digest)) != 0)
        return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest)
    {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
