#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shldisp.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>
#include "resource.h"
#include "../Rave/version.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
    const wchar_t* kTitle = L"Rave Setup";
    const wchar_t* kWindowClass = L"RaveSetupWindow";
    const wchar_t* kCheckClass = L"RaveSetupCheck";
    const wchar_t* kUninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Rave";
    const wchar_t* kAppKey = L"SOFTWARE\\Rave";
    const UINT WM_INSTALL_STATUS = WM_APP + 1;
    const UINT WM_INSTALL_DONE = WM_APP + 2;

    enum Page { PageWelcome, PageLicense, PageLocation, PageOptions, PageInstall, PageFinish };

    enum ControlId
    {
        ID_BACK = 100,
        ID_NEXT = 101,
        ID_CANCEL = IDCANCEL,
        ID_LICENSE = 110,
        ID_ACCEPT = 111,
        ID_PATH = 112,
        ID_BROWSE = 113,
        ID_STARTMENU = 114,
        ID_DESKTOP = 115,
        ID_DETAILS = 116,
        ID_LAUNCH = 117,
    };

    const int kWidth = 500;
    const int kHeight = 386;
    const int kBand = 339;
    const int kHeader = 58;
    const int kWatermark = 164;

    HINSTANCE instance;
    HWND mainWindow;
    int dpi = 96;

    HFONT bodyFont, boldFont, titleFont;
    HICON appIcon, appIconSmall, headerIcon;
    HBITMAP watermark;

    struct Theme
    {
        bool dark = false;
        COLORREF content, panel, text, subtext, line, lineLight, field, fieldText, accent, track, trackBorder, boxBorder, check;
        HBRUSH contentBrush = nullptr, panelBrush = nullptr, fieldBrush = nullptr;
    } theme;

    HWND licenseEdit, acceptCheck, pathEdit, browseButton, startMenuCheck, desktopCheck, detailsList, launchCheck;
    HWND backButton, nextButton, cancelButton;

    int page = PageWelcome;
    std::wstring installDir;
    std::wstring spaceRequired, spaceAvailable;
    std::wstring status = L"Preparing...";
    int progress = 0;
    bool startMenu = true;
    bool desktop = true;
    bool installing = false;
    bool installed = false;
    bool failed = false;
    bool keyboardCues = false;

    int S(int v) { return MulDiv(v, dpi, 96); }

    int message(HWND owner, const std::wstring& body, const wchar_t* caption, UINT type);

    struct Resource
    {
        const void* data = nullptr;
        DWORD size = 0;
    };

    Resource loadResource(int id)
    {
        Resource r;
        HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!res)
            return r;
        HGLOBAL mem = LoadResource(instance, res);
        if (!mem)
            return r;
        r.data = LockResource(mem);
        r.size = SizeofResource(instance, res);
        return r;
    }

    std::wstring knownFolder(REFKNOWNFOLDERID id)
    {
        std::wstring out;
        PWSTR raw = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
            out = raw;
        CoTaskMemFree(raw);
        return out;
    }

    std::wstring selfPath()
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

    std::wstring regRead(HKEY root, const wchar_t* key, const wchar_t* value, DWORD flags = RRF_SUBKEY_WOW6464KEY)
    {
        wchar_t buf[MAX_PATH * 2]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | flags, nullptr, buf, &size) == ERROR_SUCCESS)
            return buf;
        return {};
    }

    void regWrite(HKEY key, const wchar_t* name, const std::wstring& value)
    {
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    }

    void regWrite(HKEY key, const wchar_t* name, DWORD value)
    {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    }

    bool ensureDir(const std::wstring& dir)
    {
        int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
    }

    bool writeFile(const std::wstring& path, const void* data, DWORD size)
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                bool ok = WriteFile(h, data, size, &written, nullptr) && written == size;
                CloseHandle(h);
                return ok;
            }
            Sleep(150);
        }
        return false;
    }

    bool deleteFileRetry(const std::wstring& path)
    {
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
                return true;
            Sleep(200);
        }
        return false;
    }

    void removeTree(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..")
                    continue;
                std::wstring full = dir + L"\\" + name;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    removeTree(full);
                else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RemoveDirectoryW(full.c_str());
                else
                {
                    SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(full.c_str());
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }

    std::vector<DWORD> raveProcesses()
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
                if (_wcsicmp(pe.szExeFile, L"Rave.exe") == 0)
                    result.push_back(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return result;
    }

    bool closeRave(HWND owner)
    {
        while (!raveProcesses().empty())
        {
            int answer = message(owner, L"Rave is currently running.\n\nClick OK to close Rave and continue, or Cancel to stop.", kTitle, MB_OKCANCEL | MB_ICONWARNING);
            if (answer != IDOK)
                return false;
            for (DWORD pid : raveProcesses())
            {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
                if (h)
                {
                    TerminateProcess(h, 0);
                    WaitForSingleObject(h, 5000);
                    CloseHandle(h);
                }
            }
        }
        return true;
    }

    bool createShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& dir)
    {
        ComPtr<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            return false;
        link->SetPath(target.c_str());
        link->SetWorkingDirectory(dir.c_str());
        link->SetIconLocation(target.c_str(), -2);
        link->SetDescription(L"Rave");
        ComPtr<IPersistFile> file;
        if (FAILED(link.As(&file)))
            return false;
        return SUCCEEDED(file->Save(lnk.c_str(), TRUE));
    }

    std::wstring startMenuShortcut() { return knownFolder(FOLDERID_CommonPrograms) + L"\\Rave.lnk"; }
    std::wstring desktopShortcut() { return knownFolder(FOLDERID_PublicDesktop) + L"\\Rave.lnk"; }

    bool shellExecuteAsDesktop(const std::wstring& exe, const std::wstring& dir)
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
        ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> view;
        ComPtr<IDispatch> background;
        ComPtr<IShellFolderViewDual> folderView;
        ComPtr<IDispatch> application;
        ComPtr<IShellDispatch2> shell;
        if (FAILED(dispatch.As(&provider)) ||
            FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
            FAILED(browser->QueryActiveShellView(&view)) ||
            FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) ||
            FAILED(background.As(&folderView)) ||
            FAILED(folderView->get_Application(&application)) ||
            FAILED(application.As(&shell)))
            return false;

        BSTR file = SysAllocString(exe.c_str());
        VARIANT vArgs{}, vDir{}, vOp{}, vShow{};
        vArgs.vt = VT_BSTR;
        vArgs.bstrVal = SysAllocString(L"");
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

    void launchRave()
    {
        std::wstring exe = installDir + L"\\Rave.exe";
        if (!shellExecuteAsDesktop(exe, installDir))
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, installDir.c_str(), SW_SHOWNORMAL);
    }

    std::wstring formatSize(ULONGLONG bytes)
    {
        wchar_t buf[64];
        StrFormatByteSizeW((LONGLONG)bytes, buf, 64);
        return buf;
    }

    std::wstring defaultDir()
    {
        std::wstring existing = regRead(HKEY_LOCAL_MACHINE, kAppKey, L"InstallPath");
        if (!existing.empty())
            return existing;
        return knownFolder(FOLDERID_ProgramFiles) + L"\\Rave";
    }

    enum class AppMode { Default, AllowDark, ForceDark, ForceLight };
    using SetPreferredAppModeFn = AppMode(WINAPI*)(AppMode);
    using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
    using RefreshPolicyFn = void(WINAPI*)();

    AllowDarkModeForWindowFn allowDarkModeForWindow = nullptr;

    DWORD windowsBuild()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto fn = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
        if (fn && fn(&info) == 0)
            return info.dwBuildNumber;
        return 0;
    }

    void initDarkModeSupport()
    {
        if (windowsBuild() < 18362)
            return;
        HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!ux)
            return;
        auto setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        auto refresh = (RefreshPolicyFn)GetProcAddress(ux, MAKEINTRESOURCEA(104));
        allowDarkModeForWindow = (AllowDarkModeForWindowFn)GetProcAddress(ux, MAKEINTRESOURCEA(133));
        if (setMode)
            setMode(AppMode::AllowDark);
        if (refresh)
            refresh();
    }

    bool systemUsesDarkApps()
    {
        DWORD value = 1, size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            return false;
        HIGHCONTRAST hc{ sizeof(hc) };
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON))
            return false;
        return value == 0;
    }

    void themeControl(HWND hwnd, const wchar_t* darkTheme)
    {
        if (!hwnd)
            return;
        if (allowDarkModeForWindow)
            allowDarkModeForWindow(hwnd, theme.dark);
        SetWindowTheme(hwnd, theme.dark ? darkTheme : L"Explorer", nullptr);
        SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
    }

    void applyTheme()
    {
        theme.dark = systemUsesDarkApps();
        if (theme.dark)
        {
            theme.content = RGB(32, 32, 32);
            theme.panel = RGB(43, 43, 43);
            theme.text = RGB(255, 255, 255);
            theme.subtext = RGB(206, 206, 206);
            theme.line = RGB(64, 64, 64);
            theme.lineLight = RGB(64, 64, 64);
            theme.field = RGB(25, 25, 25);
            theme.fieldText = RGB(240, 240, 240);
            theme.track = RGB(55, 55, 55);
            theme.trackBorder = RGB(80, 80, 80);
            theme.boxBorder = RGB(170, 170, 170);
            theme.check = RGB(15, 15, 15);
        }
        else
        {
            theme.content = GetSysColor(COLOR_3DFACE);
            theme.panel = RGB(255, 255, 255);
            theme.text = RGB(0, 0, 0);
            theme.subtext = RGB(0, 0, 0);
            theme.line = RGB(160, 160, 160);
            theme.lineLight = RGB(255, 255, 255);
            theme.field = RGB(255, 255, 255);
            theme.fieldText = RGB(0, 0, 0);
            theme.track = RGB(230, 230, 230);
            theme.trackBorder = RGB(188, 188, 188);
            theme.boxBorder = RGB(51, 51, 51);
            theme.check = RGB(15, 15, 15);
        }
        theme.accent = RGB(106, 221, 187);

        if (theme.contentBrush) DeleteObject(theme.contentBrush);
        if (theme.panelBrush) DeleteObject(theme.panelBrush);
        if (theme.fieldBrush) DeleteObject(theme.fieldBrush);
        theme.contentBrush = CreateSolidBrush(theme.content);
        theme.panelBrush = CreateSolidBrush(theme.panel);
        theme.fieldBrush = CreateSolidBrush(theme.field);

        if (!mainWindow)
            return;

        if (allowDarkModeForWindow)
            allowDarkModeForWindow(mainWindow, theme.dark);
        BOOL dark = theme.dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(mainWindow, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(mainWindow, 19, &dark, sizeof(dark));

        themeControl(licenseEdit, L"DarkMode_Explorer");
        themeControl(pathEdit, L"DarkMode_CFD");
        themeControl(detailsList, L"DarkMode_Explorer");
        themeControl(browseButton, L"DarkMode_Explorer");
        themeControl(backButton, L"DarkMode_Explorer");
        themeControl(nextButton, L"DarkMode_Explorer");
        themeControl(cancelButton, L"DarkMode_Explorer");

        RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    bool exteriorPage()
    {
        return page == PageWelcome || page == PageFinish;
    }

    COLORREF backgroundAt(HWND child)
    {
        RECT rc;
        GetWindowRect(child, &rc);
        MapWindowPoints(nullptr, mainWindow, (POINT*)&rc, 2);
        if (rc.top >= S(kBand))
            return theme.content;
        if (exteriorPage())
            return theme.panel;
        return rc.top < S(kHeader) ? theme.panel : theme.content;
    }

    HBITMAP makeWatermark(int w, int h)
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!bmp)
            return nullptr;

        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, bmp);

        for (int y = 0; y < h; ++y)
        {
            float t = (float)y / (float)h;
            HBRUSH brush = CreateSolidBrush(RGB((int)(14 + 6 * t), (int)(20 + 24 * t), (int)(19 + 19 * t)));
            RECT line{ 0, y, w, y + 1 };
            FillRect(dc, &line, brush);
            DeleteObject(brush);
        }

        int iconSize = w * 2 / 3;
        HICON big = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON, iconSize, iconSize, 0);
        if (big)
        {
            DrawIconEx(dc, (w - iconSize) / 2, h / 2 - iconSize, big, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            DestroyIcon(big);
        }

        SetBkMode(dc, TRANSPARENT);
        int height = -MulDiv(h, 9, 100);
        HFONT font = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetTextColor(dc, RGB(228, 228, 228));
        RECT text{ 0, h / 2 + h / 40, w, h / 2 + h / 6 };
        DrawTextW(dc, L"Rave", -1, &text, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, oldFont);
        DeleteObject(font);

        font = CreateFontW(height / 2, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        oldFont = SelectObject(dc, font);
        SetTextColor(dc, RGB(106, 221, 187));
        RECT ver{ 0, h / 2 + h / 6, w, h / 2 + h / 6 + h / 14 };
        DrawTextW(dc, L"V" RAVE_VERSION_WSTR, -1, &ver, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, oldFont);
        DeleteObject(font);

        SelectObject(dc, old);
        DeleteDC(dc);
        return bmp;
    }

    void fill(HDC dc, RECT rc, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }

    void hline(HDC dc, int x0, int x1, int y, COLORREF color)
    {
        fill(dc, { x0, y, x1, y + 1 }, color);
    }

    void frame(HDC dc, RECT rc, COLORREF color)
    {
        hline(dc, rc.left, rc.right, rc.top, color);
        hline(dc, rc.left, rc.right, rc.bottom - 1, color);
        fill(dc, { rc.left, rc.top, rc.left + 1, rc.bottom }, color);
        fill(dc, { rc.right - 1, rc.top, rc.right, rc.bottom }, color);
    }

    void text(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& str, UINT flags = DT_WORDBREAK)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, str.c_str(), (int)str.size(), &rc, flags | DT_NOPREFIX);
        SelectObject(dc, old);
    }

    RECT R(int x, int y, int w, int h)
    {
        return { S(x), S(y), S(x + w), S(y + h) };
    }

    const wchar_t* kMessageClass = L"RaveSetupMessage";

    struct MessageState
    {
        std::wstring body;
        HICON icon = nullptr;
        RECT textRect{};
        int band = 0;
        int defaultId = IDOK;
        int cancelId = IDOK;
        int result = 0;
        bool done = false;
        std::vector<HWND> buttons;
    };

    void paintMessage(HWND hwnd, HDC dc)
    {
        MessageState* st = (MessageState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        RECT rc;
        GetClientRect(hwnd, &rc);
        fill(dc, rc, theme.content);
        if (!st)
            return;
        fill(dc, { 0, 0, rc.right, rc.bottom - st->band }, theme.panel);
        hline(dc, 0, rc.right, rc.bottom - st->band, theme.line);
        if (st->icon)
            DrawIconEx(dc, S(24), S(24), st->icon, S(32), S(32), 0, nullptr, DI_NORMAL);
        text(dc, bodyFont, theme.text, st->textRect, st->body, DT_WORDBREAK);
    }

    LRESULT CALLBACK messageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        MessageState* st = (MessageState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(dc, bmp);
            paintMessage(hwnd, dc);
            BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            paintMessage(hwnd, (HDC)wp);
            return 0;
        case WM_CTLCOLORBTN:
            return (LRESULT)theme.contentBrush;
        case DM_GETDEFID:
            return st ? MAKELRESULT(st->defaultId, DC_HASDEFID) : 0;
        case WM_COMMAND:
            if (st && HIWORD(wp) == BN_CLICKED)
            {
                int id = LOWORD(wp);
                if (id == IDCANCEL)
                    id = st->cancelId;
                st->result = id;
                st->done = true;
            }
            return 0;
        case WM_CLOSE:
            if (st)
            {
                st->result = st->cancelId;
                st->done = true;
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    int message(HWND owner, const std::wstring& body, const wchar_t* caption, UINT type)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = messageProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kMessageClass;
            RegisterClassExW(&wc);
            registered = true;
        }

        MessageState st;
        st.body = body;

        struct Button { int id; const wchar_t* label; };
        std::vector<Button> buttons;
        switch (type & MB_TYPEMASK)
        {
        case MB_YESNO:
            buttons = { { IDYES, L"&Yes" }, { IDNO, L"&No" } };
            st.cancelId = IDNO;
            break;
        case MB_OKCANCEL:
            buttons = { { IDOK, L"OK" }, { IDCANCEL, L"Cancel" } };
            st.cancelId = IDCANCEL;
            break;
        default:
            buttons = { { IDOK, L"OK" } };
            st.cancelId = IDOK;
            break;
        }
        int defIndex = ((type & MB_DEFMASK) == MB_DEFBUTTON2 && buttons.size() > 1) ? 1 : 0;
        st.defaultId = buttons[defIndex].id;

        LPCWSTR iconId = nullptr;
        UINT sound = MB_OK;
        switch (type & MB_ICONMASK)
        {
        case MB_ICONQUESTION: iconId = IDI_QUESTION; sound = MB_ICONQUESTION; break;
        case MB_ICONWARNING: iconId = IDI_WARNING; sound = MB_ICONWARNING; break;
        case MB_ICONERROR: iconId = IDI_ERROR; sound = MB_ICONERROR; break;
        case MB_ICONINFORMATION: iconId = IDI_INFORMATION; sound = MB_ICONINFORMATION; break;
        }
        if (iconId)
            LoadIconWithScaleDown(nullptr, iconId, S(32), S(32), &st.icon);

        HDC screen = GetDC(nullptr);
        HGDIOBJ oldFont = SelectObject(screen, bodyFont);
        RECT measure{ 0, 0, S(380), 0 };
        DrawTextW(screen, body.c_str(), (int)body.size(), &measure, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        SelectObject(screen, oldFont);
        ReleaseDC(nullptr, screen);

        int textX = st.icon ? S(24 + 32 + 14) : S(24);
        int textW = measure.right - measure.left;
        int textH = measure.bottom - measure.top;
        int top = S(24);
        int contentH = std::max(textH, st.icon ? S(32) : 0);
        int textY = top + (contentH - textH) / 2;

        int bw = S(88), bh = S(26), gap = S(8), margin = S(12);
        int buttonsW = (int)buttons.size() * bw + ((int)buttons.size() - 1) * gap;
        st.band = S(50);
        int clientW = std::max(textX + textW + S(24), buttonsW + margin * 2);
        clientW = std::max(clientW, S(300));
        int clientH = top + contentH + S(24) + st.band;
        st.textRect = { textX, textY, textX + textW + S(2), textY + textH };

        DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | (owner ? 0 : WS_EX_TOPMOST);
        RECT wr{ 0, 0, clientW, clientH };
        AdjustWindowRectEx(&wr, style, FALSE, exStyle);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;

        RECT area{};
        if (owner && IsWindow(owner))
            GetWindowRect(owner, &area);
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        int x = area.left + (area.right - area.left - w) / 2;
        int y = area.top + (area.bottom - area.top - h) / 2;

        HWND hwnd = CreateWindowExW(exStyle, kMessageClass, caption, style, x, y, w, h, owner, nullptr, instance, nullptr);
        if (!hwnd)
            return MessageBoxW(owner, body.c_str(), caption, type);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIconSmall);

        if (allowDarkModeForWindow)
            allowDarkModeForWindow(hwnd, theme.dark);
        BOOL dark = theme.dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));

        int bx = clientW - margin - buttonsW;
        int by = clientH - st.band + (st.band - bh) / 2;
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            DWORD bstyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ((int)i == defIndex ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            HWND b = CreateWindowExW(0, L"BUTTON", buttons[i].label, bstyle, bx + (int)i * (bw + gap), by, bw, bh, hwnd, (HMENU)(INT_PTR)buttons[i].id, instance, nullptr);
            SendMessageW(b, WM_SETFONT, (WPARAM)bodyFont, TRUE);
            themeControl(b, L"DarkMode_Explorer");
            st.buttons.push_back(b);
        }

        bool ownerWasEnabled = owner && IsWindowEnabled(owner);
        if (ownerWasEnabled)
            EnableWindow(owner, FALSE);

        MessageBeep(sound);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
        SetFocus(st.buttons[defIndex]);

        MSG msg;
        bool quit = false;
        int quitCode = 0;
        while (!st.done)
        {
            BOOL got = GetMessageW(&msg, nullptr, 0, 0);
            if (got <= 0)
            {
                quit = got == 0;
                quitCode = (int)msg.wParam;
                st.result = st.cancelId;
                break;
            }
            if (!IsDialogMessageW(hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (ownerWasEnabled)
            EnableWindow(owner, TRUE);
        DestroyWindow(hwnd);
        if (owner && IsWindow(owner))
            SetForegroundWindow(owner);
        if (st.icon)
            DestroyIcon(st.icon);
        if (quit)
            PostQuitMessage(quitCode);
        return st.result;
    }

    void paint(HDC dc)
    {
        RECT client;
        GetClientRect(mainWindow, &client);
        fill(dc, client, theme.content);

        if (exteriorPage())
        {
            fill(dc, { 0, 0, client.right, S(kBand) }, theme.panel);
            if (watermark)
            {
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(mem, watermark);
                BitBlt(dc, 0, 0, S(kWatermark), S(kBand), mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }

            if (page == PageWelcome)
            {
                text(dc, titleFont, theme.text, R(176, 16, 312, 50), L"Welcome to Rave V" RAVE_VERSION_WSTR L" Setup");
                text(dc, bodyFont, theme.text, R(176, 70, 312, 250),
                    L"This wizard will guide you through the installation of Rave V" RAVE_VERSION_WSTR L".\r\n\r\n"
                    L"Rave is a Roblox launcher that always downloads and starts the latest Roblox version.\r\n\r\n"
                    L"It is recommended that you close all other applications before starting Setup.\r\n\r\n"
                    L"Click Next to continue.");
            }
            else
            {
                text(dc, titleFont, theme.text, R(176, 16, 312, 50), L"Completing Rave V" RAVE_VERSION_WSTR L" Setup");
                text(dc, bodyFont, theme.text, R(176, 70, 312, 120),
                    L"Rave V" RAVE_VERSION_WSTR L" has been installed on your computer.\r\n\r\nClick Finish to close Setup.");
            }
        }
        else
        {
            fill(dc, { 0, 0, client.right, S(kHeader) }, theme.panel);
            hline(dc, 0, client.right, S(kHeader), theme.line);
            hline(dc, 0, client.right, S(kHeader) + 1, theme.lineLight);

            const wchar_t* title = L"";
            std::wstring subtitle;
            switch (page)
            {
            case PageLicense: title = L"License Agreement"; subtitle = L"Please review the license terms before installing Rave V" RAVE_VERSION_WSTR L"."; break;
            case PageLocation: title = L"Choose Install Location"; subtitle = L"Choose the folder in which to install Rave V" RAVE_VERSION_WSTR L"."; break;
            case PageOptions: title = L"Choose Additional Tasks"; subtitle = L"Which additional tasks should be performed?"; break;
            case PageInstall: title = L"Installing"; subtitle = L"Please wait while Rave V" RAVE_VERSION_WSTR L" is being installed."; break;
            }
            text(dc, boldFont, theme.text, R(20, 11, 400, 18), title, DT_SINGLELINE);
            text(dc, bodyFont, theme.subtext, R(34, 30, 390, 18), subtitle, DT_SINGLELINE | DT_END_ELLIPSIS);
            if (headerIcon)
                DrawIconEx(dc, S(kWidth - 49 - 8), S(4), headerIcon, S(49), S(49), 0, nullptr, DI_NORMAL);

            if (page == PageLicense)
            {
                text(dc, bodyFont, theme.text, R(36, 70, 428, 18), L"Press Page Down to see the rest of the agreement.", DT_SINGLELINE);
                RECT box = R(36, 90, 428, 192);
                fill(dc, box, theme.field);
                frame(dc, box, theme.trackBorder);
            }
            else if (page == PageLocation)
            {
                text(dc, bodyFont, theme.text, R(36, 70, 428, 50), L"Setup will install Rave in the following folder. To install in a different folder, click Browse and select another folder. Click Next to continue.");

                RECT group = R(36, 134, 428, 62);
                frame(dc, group, theme.line);
                HGDIOBJ old = SelectObject(dc, bodyFont);
                SIZE size;
                GetTextExtentPoint32W(dc, L"Destination Folder", 18, &size);
                SelectObject(dc, old);
                RECT label{ group.left + S(8), group.top - size.cy / 2, group.left + S(8) + size.cx + S(6), group.top + size.cy / 2 + 1 };
                fill(dc, label, theme.content);
                text(dc, bodyFont, theme.text, { label.left + S(3), label.top, label.right, label.bottom }, L"Destination Folder", DT_SINGLELINE);

                text(dc, bodyFont, theme.text, R(36, 212, 428, 18), spaceRequired, DT_SINGLELINE);
                text(dc, bodyFont, theme.text, R(36, 232, 428, 18), spaceAvailable, DT_SINGLELINE);
            }
            else if (page == PageOptions)
            {
                text(dc, bodyFont, theme.text, R(36, 70, 428, 40), L"Select the additional tasks you would like Setup to perform while installing Rave, then click Install.");
            }
            else if (page == PageInstall)
            {
                text(dc, bodyFont, theme.text, R(36, 70, 428, 18), status, DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT track = R(36, 92, 428, 16);
                fill(dc, track, theme.track);
                frame(dc, track, theme.trackBorder);
                RECT bar = { track.left + 1, track.top + 1, track.left + 1 + (track.right - track.left - 2) * progress / 100, track.bottom - 1 };
                if (bar.right > bar.left)
                    fill(dc, bar, theme.accent);
            }
        }

        hline(dc, 0, client.right, S(kBand), theme.line);
        hline(dc, 0, client.right, S(kBand) + 1, theme.lineLight);
    }

    void updateSpace()
    {
        Resource payload = loadResource(IDR_PAYLOAD);
        spaceRequired = L"Space required: " + formatSize((ULONGLONG)payload.size * 2);

        wchar_t path[MAX_PATH * 2]{};
        GetWindowTextW(pathEdit, path, MAX_PATH * 2);
        std::wstring probe = path;
        ULARGE_INTEGER freeBytes{};
        bool found = false;
        while (!probe.empty())
        {
            if (GetDiskFreeSpaceExW(probe.c_str(), &freeBytes, nullptr, nullptr))
            {
                found = true;
                break;
            }
            size_t slash = probe.find_last_of(L"\\/");
            if (slash == std::wstring::npos)
                break;
            probe.resize(slash);
        }
        spaceAvailable = found ? L"Space available: " + formatSize(freeBytes.QuadPart) : L"Space available: unknown";
        RECT rc = R(36, 210, 428, 44);
        InvalidateRect(mainWindow, &rc, FALSE);
    }

    void browse()
    {
        ComPtr<IFileOpenDialog> picker;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
            return;
        DWORD options = 0;
        picker->GetOptions(&options);
        picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        picker->SetTitle(L"Select the folder to install Rave in");
        if (FAILED(picker->Show(mainWindow)))
            return;
        ComPtr<IShellItem> item;
        if (FAILED(picker->GetResult(&item)))
            return;
        PWSTR raw = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)))
            return;
        std::wstring chosen = raw;
        CoTaskMemFree(raw);
        while (!chosen.empty() && chosen.back() == L'\\')
            chosen.pop_back();
        size_t slash = chosen.find_last_of(L'\\');
        std::wstring leaf = slash == std::wstring::npos ? chosen : chosen.substr(slash + 1);
        if (_wcsicmp(leaf.c_str(), L"Rave") != 0)
            chosen += L"\\Rave";
        SetWindowTextW(pathEdit, chosen.c_str());
    }

    bool isChecked(HWND check)
    {
        return SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    LRESULT CALLBACK checkProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case BM_GETCHECK:
            return (GetWindowLongPtrW(hwnd, GWLP_USERDATA) & 1) ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, wp == BST_CHECKED ? 1 : 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_BUTTON;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case WM_SETTEXT:
        {
            LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            InvalidateRect(hwnd, nullptr, FALSE);
            return r;
        }
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
        {
            if (GetCapture() != hwnd)
                return 0;
            ReleaseCapture();
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!PtInRect(&rc, pt))
                return 0;
        }
            [[fallthrough]];
        case WM_USER:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (GetWindowLongPtrW(hwnd, GWLP_USERDATA) & 1) ^ 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
            return 0;
        case WM_KEYUP:
            if (wp == VK_SPACE)
                SendMessageW(hwnd, WM_USER, 0, 0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(dc, bmp);

            fill(dc, rc, backgroundAt(hwnd));
            bool checked = (GetWindowLongPtrW(hwnd, GWLP_USERDATA) & 1) != 0;
            int box = S(14);
            RECT boxRc{ 0, (rc.bottom - box) / 2, box, (rc.bottom - box) / 2 + box };
            if (checked)
            {
                fill(dc, boxRc, theme.accent);
                HPEN pen = CreatePen(PS_SOLID, std::max(1, S(2)), theme.check);
                HGDIOBJ oldPen = SelectObject(dc, pen);
                MoveToEx(dc, boxRc.left + box * 3 / 14, boxRc.top + box * 7 / 14, nullptr);
                LineTo(dc, boxRc.left + box * 6 / 14, boxRc.top + box * 10 / 14);
                LineTo(dc, boxRc.left + box * 11 / 14, boxRc.top + box * 4 / 14);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
            }
            else
            {
                fill(dc, boxRc, theme.field);
                frame(dc, boxRc, theme.boxBorder);
            }

            wchar_t label[256]{};
            GetWindowTextW(hwnd, label, 256);
            HFONT font = (HFONT)SendMessageW(GetParent(hwnd), WM_GETFONT, 0, 0);
            if (!font)
                font = bodyFont;
            RECT textRc{ box + S(8), 0, rc.right, rc.bottom };
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, IsWindowEnabled(hwnd) ? theme.text : theme.line);
            DrawTextW(dc, label, -1, &textRc, DT_SINGLELINE | DT_VCENTER);
            if (keyboardCues && GetFocus() == hwnd)
            {
                RECT measure = textRc;
                DrawTextW(dc, label, -1, &measure, DT_SINGLELINE | DT_VCENTER | DT_CALCRECT);
                int h = measure.bottom - measure.top;
                RECT focus{ textRc.left - S(2), (rc.bottom - h) / 2 - 1, measure.right + S(2), (rc.bottom + h) / 2 + 1 };
                SetTextColor(dc, theme.text);
                DrawFocusRect(dc, &focus);
            }
            SelectObject(dc, oldFont);

            BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBmp);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK licenseSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (msg == WM_GETDLGCODE)
            return DefSubclassProc(hwnd, msg, wp, lp) & ~DLGC_HASSETSEL;
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    HWND makeControl(const wchar_t* cls, const wchar_t* label, DWORD style, DWORD exStyle, int id, RECT rc)
    {
        HWND hwnd = CreateWindowExW(exStyle, cls, label, WS_CHILD | style, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mainWindow, (HMENU)(INT_PTR)id, instance, nullptr);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)bodyFont, TRUE);
        return hwnd;
    }

    void loadLicense()
    {
        Resource lic = loadResource(IDR_LICENSE);
        std::string raw((const char*)lic.data, lic.size);
        std::string body;
        body.reserve(raw.size() + raw.size() / 20);
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                body += '\r';
            body += raw[i];
        }
        int n = MultiByteToWideChar(CP_UTF8, 0, body.data(), (int)body.size(), nullptr, 0);
        std::wstring wide(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, body.data(), (int)body.size(), wide.data(), n);
        SendMessageW(licenseEdit, EM_SETLIMITTEXT, 0, 0);
        SetWindowTextW(licenseEdit, wide.c_str());
        SendMessageW(licenseEdit, EM_SETSEL, 0, 0);
    }

    void createControls()
    {
        RECT licenseFrame = R(36, 90, 428, 192);
        InflateRect(&licenseFrame, -1, -1);
        licenseEdit = makeControl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 0, ID_LICENSE, licenseFrame);
        SetWindowSubclass(licenseEdit, licenseSubclass, 1, 0);
        RECT inner;
        GetClientRect(licenseEdit, &inner);
        inner.left += S(6);
        inner.top += S(4);
        inner.right -= S(4);
        SendMessageW(licenseEdit, EM_SETRECTNP, 0, (LPARAM)&inner);
        acceptCheck = makeControl(kCheckClass, L"I accept the terms of the License Agreement", WS_TABSTOP, 0, ID_ACCEPT, R(36, 292, 428, 22));

        pathEdit = makeControl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_PATH, R(50, 154, 310, 24));
        browseButton = makeControl(L"BUTTON", L"B&rowse...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_BROWSE, R(370, 153, 82, 26));

        startMenuCheck = makeControl(kCheckClass, L"Create a Start Menu shortcut", WS_TABSTOP, 0, ID_STARTMENU, R(48, 116, 400, 22));
        desktopCheck = makeControl(kCheckClass, L"Create a Desktop shortcut", WS_TABSTOP, 0, ID_DESKTOP, R(48, 142, 400, 22));
        SendMessageW(startMenuCheck, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(desktopCheck, BM_SETCHECK, BST_CHECKED, 0);

        detailsList = makeControl(L"LISTBOX", L"", LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL, WS_EX_CLIENTEDGE, ID_DETAILS, R(36, 118, 428, 206));

        launchCheck = makeControl(kCheckClass, L"Launch Rave V" RAVE_VERSION_WSTR, WS_TABSTOP, 0, ID_LAUNCH, R(176, 200, 300, 22));
        SendMessageW(launchCheck, BM_SETCHECK, BST_CHECKED, 0);

        backButton = makeControl(L"BUTTON", L"< &Back", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, 0, ID_BACK, R(257, 351, 75, 23));
        nextButton = makeControl(L"BUTTON", L"&Next >", BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, 0, ID_NEXT, R(332, 351, 75, 23));
        cancelButton = makeControl(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, 0, ID_CANCEL, R(414, 351, 75, 23));

        loadLicense();
        SetWindowTextW(pathEdit, defaultDir().c_str());
        updateSpace();
    }

    void showPage(int next)
    {
        page = next;
        auto show = [](HWND hwnd, bool visible) { ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE); };
        show(licenseEdit, page == PageLicense);
        show(acceptCheck, page == PageLicense);
        show(pathEdit, page == PageLocation);
        show(browseButton, page == PageLocation);
        show(startMenuCheck, page == PageOptions);
        show(desktopCheck, page == PageOptions);
        show(detailsList, page == PageInstall);
        show(launchCheck, page == PageFinish);

        EnableWindow(backButton, page == PageLicense || page == PageLocation || page == PageOptions);
        EnableWindow(nextButton, page != PageInstall && (page != PageLicense || isChecked(acceptCheck)));
        EnableWindow(cancelButton, page != PageInstall && page != PageFinish);
        SetWindowTextW(nextButton, page == PageOptions ? L"&Install" : page == PageFinish ? L"&Finish" : L"&Next >");
        SetWindowTextW(cancelButton, L"Cancel");

        if (page == PageLicense)
        {
            SendMessageW(licenseEdit, EM_SETSEL, 0, 0);
            SetFocus(acceptCheck);
        }
        else if (page == PageLocation)
            SetFocus(pathEdit);
        else if (IsWindowEnabled(nextButton))
            SetFocus(nextButton);

        RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    void post(UINT msg, const std::wstring& str, WPARAM value)
    {
        std::wstring* copy = new std::wstring(str);
        if (!PostMessageW(mainWindow, msg, value, (LPARAM)copy))
            delete copy;
    }

    void installWorker()
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto step = [](const std::wstring& str, int percent) { post(WM_INSTALL_STATUS, str, (WPARAM)percent); };
        auto fail = [](const std::wstring& str) {
            post(WM_INSTALL_DONE, str, 0);
            CoUninitialize();
        };

        step(L"Checking for a running Rave...", 5);
        if (!closeRave(mainWindow))
        {
            fail(L"Setup was cancelled because Rave is still running.");
            return;
        }

        step(L"Create folder: " + installDir, 15);
        if (!ensureDir(installDir))
        {
            fail(L"Could not create the folder:\r\n" + installDir);
            return;
        }

        Resource payload = loadResource(IDR_PAYLOAD);
        std::wstring exe = installDir + L"\\Rave.exe";
        step(L"Extract: Rave.exe", 35);
        if (!payload.data || !writeFile(exe, payload.data, payload.size))
        {
            fail(L"Could not write:\r\n" + exe);
            return;
        }

        std::wstring uninstaller = installDir + L"\\uninstall.exe";
        step(L"Create uninstaller: uninstall.exe", 60);
        if (!CopyFileW(selfPath().c_str(), uninstaller.c_str(), FALSE))
        {
            fail(L"Could not write:\r\n" + uninstaller);
            return;
        }

        Resource license = loadResource(IDR_LICENSE);
        std::wstring licenseFile = installDir + L"\\LICENSE.txt";
        step(L"Extract: LICENSE.txt", 68);
        if (!license.data || !writeFile(licenseFile, license.data, license.size))
        {
            fail(L"Could not write:\r\n" + licenseFile);
            return;
        }

        step(L"Create shortcuts", 75);
        if (startMenu)
            createShortcut(startMenuShortcut(), exe, installDir);
        else
            DeleteFileW(startMenuShortcut().c_str());
        if (desktop)
            createShortcut(desktopShortcut(), exe, installDir);
        else
            DeleteFileW(desktopShortcut().c_str());

        step(L"Register Rave in Apps and Features", 90);
        HKEY key;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            regWrite(key, L"DisplayName", L"Rave");
            regWrite(key, L"DisplayVersion", RAVE_VERSION_WSTR);
            regWrite(key, L"Publisher", RAVE_PUBLISHER);
            regWrite(key, L"DisplayIcon", exe + L",0");
            regWrite(key, L"InstallLocation", installDir);
            regWrite(key, L"UninstallString", L"\"" + uninstaller + L"\"");
            regWrite(key, L"QuietUninstallString", L"\"" + uninstaller + L"\"");
            regWrite(key, L"URLInfoAbout", RAVE_PUBLIC_URL);
            regWrite(key, L"HelpLink", RAVE_PUBLIC_URL);
            regWrite(key, L"EstimatedSize", (DWORD)((payload.size * 2ull) / 1024));
            regWrite(key, L"NoModify", 1);
            regWrite(key, L"NoRepair", 1);
            RegCloseKey(key);
        }
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kAppKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            regWrite(key, L"InstallPath", installDir);
            regWrite(key, L"Version", RAVE_VERSION_WSTR);
            RegCloseKey(key);
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        step(L"Completed", 100);
        post(WM_INSTALL_DONE, L"", 1);
        CoUninitialize();
    }

    bool validateLocation()
    {
        wchar_t path[MAX_PATH * 2]{};
        GetWindowTextW(pathEdit, path, MAX_PATH * 2);
        std::wstring dir = path;
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L' '))
            dir.pop_back();
        if (dir.size() < 3 || PathIsRelativeW(dir.c_str()) || dir.find_first_of(L"*?\"<>|") != std::wstring::npos || PathIsRootW(dir.c_str()))
        {
            message(mainWindow, L"Please choose a valid folder to install Rave in.", kTitle, MB_OK | MB_ICONWARNING);
            return false;
        }
        installDir = dir;
        return true;
    }

    void finish()
    {
        bool launch = isChecked(launchCheck);
        DestroyWindow(mainWindow);
        if (launch)
            launchRave();
    }

    void cancel()
    {
        if (installing)
        {
            MessageBeep(MB_OK);
            return;
        }
        if (page == PageFinish)
        {
            finish();
            return;
        }
        if (failed || message(mainWindow, L"Are you sure you want to quit Rave Setup?", kTitle, MB_YESNO | MB_ICONQUESTION) == IDYES)
            DestroyWindow(mainWindow);
    }

    void onCommand(int id, int code)
    {
        switch (id)
        {
        case ID_NEXT:
            if (!IsWindowEnabled(nextButton))
                return;
            if (page == PageWelcome)
                showPage(PageLicense);
            else if (page == PageLicense && isChecked(acceptCheck))
                showPage(PageLocation);
            else if (page == PageLocation && validateLocation())
                showPage(PageOptions);
            else if (page == PageOptions)
            {
                startMenu = isChecked(startMenuCheck);
                desktop = isChecked(desktopCheck);
                installing = true;
                progress = 0;
                status = L"Preparing...";
                showPage(PageInstall);
                std::thread(installWorker).detach();
            }
            else if (page == PageFinish)
                finish();
            break;
        case ID_BACK:
            if (!IsWindowEnabled(backButton))
                return;
            if (page == PageLicense || page == PageLocation || page == PageOptions)
                showPage(page - 1);
            break;
        case ID_CANCEL:
            cancel();
            break;
        case ID_ACCEPT:
            if (code == BN_CLICKED)
                EnableWindow(nextButton, isChecked(acceptCheck));
            break;
        case ID_BROWSE:
            if (code == BN_CLICKED)
                browse();
            break;
        case ID_PATH:
            if (code == EN_CHANGE)
                updateSpace();
            break;
        }
    }

    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CREATE:
            mainWindow = hwnd;
            createControls();
            applyTheme();
            showPage(PageWelcome);
            return 0;
        case WM_GETFONT:
            return (LRESULT)bodyFont;
        case DM_GETDEFID:
            return MAKELRESULT(ID_NEXT, DC_HASDEFID);
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(dc, bmp);
            paint(dc);
            BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            paint((HDC)wp);
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC:
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, theme.fieldText);
            SetBkColor(dc, theme.field);
            return (LRESULT)theme.fieldBrush;
        }
        case WM_CTLCOLORBTN:
            return (LRESULT)theme.contentBrush;
        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_INSTALL_STATUS:
        {
            std::wstring* str = (std::wstring*)lp;
            status = *str;
            progress = (int)wp;
            LRESULT index = SendMessageW(detailsList, LB_ADDSTRING, 0, (LPARAM)str->c_str());
            SendMessageW(detailsList, LB_SETTOPINDEX, (WPARAM)index, 0);
            RECT rc = R(36, 66, 428, 46);
            InvalidateRect(hwnd, &rc, FALSE);
            delete str;
            return 0;
        }
        case WM_INSTALL_DONE:
        {
            std::wstring* str = (std::wstring*)lp;
            installing = false;
            if (wp)
            {
                installed = true;
                showPage(PageFinish);
            }
            else
            {
                failed = true;
                status = L"Installation failed";
                SendMessageW(detailsList, LB_ADDSTRING, 0, (LPARAM)str->c_str());
                EnableWindow(cancelButton, TRUE);
                SetWindowTextW(cancelButton, L"&Close");
                InvalidateRect(hwnd, nullptr, FALSE);
                message(hwnd, str->c_str(), kTitle, MB_OK | MB_ICONERROR);
            }
            delete str;
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0)
                applyTheme();
            break;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            applyTheme();
            break;
        case WM_CLOSE:
            cancel();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void createFonts()
    {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont;
        bodyFont = CreateFontIndirectW(&lf);
        lf.lfWeight = FW_BOLD;
        boldFont = CreateFontIndirectW(&lf);
        titleFont = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, lf.lfFaceName);
    }

    void initUi()
    {
        HDC screen = GetDC(nullptr);
        dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);

        initDarkModeSupport();
        applyTheme();
        createFonts();
    }

    void deleteSelfSoon(const std::wstring& path)
    {
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        std::wstring cmdExe = std::wstring(system) + L"\\cmd.exe";
        std::wstring cmd = L"\"" + cmdExe + L"\" /c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + path + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(cmdExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, system, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    int runInstaller()
    {
        watermark = makeWatermark(S(kWatermark), S(kBand));
        headerIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_RAVE), IMAGE_ICON, S(49), S(49), 0);

        WNDCLASSEXW check{ sizeof(check) };
        check.style = CS_HREDRAW | CS_VREDRAW;
        check.lpfnWndProc = checkProc;
        check.hInstance = instance;
        check.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        check.lpszClassName = kCheckClass;
        RegisterClassExW(&check);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = windowProc;
        wc.hInstance = instance;
        wc.hIcon = appIcon;
        wc.hIconSm = appIconSmall;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        DWORD exStyle = WS_EX_CONTROLPARENT;
        RECT rc{ 0, 0, S(kWidth), S(kHeight) };
        AdjustWindowRectEx(&rc, style, FALSE, exStyle);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int x = work.left + (work.right - work.left - w) / 2;
        int y = work.top + (work.bottom - work.top - h) / 2;

        HWND hwnd = CreateWindowExW(exStyle, kWindowClass, kTitle, style, x, y, w, h, nullptr, nullptr, instance, nullptr);
        if (!hwnd)
            return 1;
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIconSmall);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!keyboardCues && msg.message == WM_KEYDOWN && (msg.wParam == VK_TAB || msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT || msg.wParam == VK_UP || msg.wParam == VK_DOWN))
            {
                keyboardCues = true;
                if (HWND focused = GetFocus())
                    InvalidateRect(focused, nullptr, FALSE);
            }
            if (msg.message == WM_KEYDOWN && page == PageLicense && (msg.wParam == VK_NEXT || msg.wParam == VK_PRIOR) && GetFocus() != licenseEdit)
            {
                SendMessageW(licenseEdit, EM_SCROLL, msg.wParam == VK_NEXT ? SB_PAGEDOWN : SB_PAGEUP, 0);
                continue;
            }
            if (!IsDialogMessageW(mainWindow, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        DeleteObject(watermark);
        DeleteObject(bodyFont);
        DeleteObject(boldFont);
        DeleteObject(titleFont);
        return installed ? 0 : 1;
    }

    int runUninstaller(const std::wstring& dir)
    {
        std::wstring exe = dir + L"\\Rave.exe";
        if (message(nullptr, L"Are you sure you want to completely remove Rave and all of its components?", L"Rave Uninstall", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 1;

        if (!closeRave(nullptr))
            return 1;

        deleteFileRetry(exe);
        deleteFileRetry(dir + L"\\uninstall.exe");
        deleteFileRetry(dir + L"\\LICENSE.txt");
        RemoveDirectoryW(dir.c_str());

        DeleteFileW(startMenuShortcut().c_str());
        DeleteFileW(desktopShortcut().c_str());

        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kAppKey, KEY_WOW64_64KEY, 0);

        std::wstring data = knownFolder(FOLDERID_LocalAppData) + L"\\Rave";
        if (GetFileAttributesW(data.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring question = L"Do you also want to remove the Rave user data?\n\nThis deletes the Roblox versions and FFlags Rave saved in:\n" + data;
            if (message(nullptr, question.c_str(), L"Rave Uninstall", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                removeTree(data);
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        message(nullptr, L"Rave was successfully removed from your computer.", L"Rave Uninstall", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    int startUninstall()
    {
        std::wstring self = selfPath();
        std::wstring dir = regRead(HKEY_LOCAL_MACHINE, kAppKey, L"InstallPath");
        if (dir.empty())
            dir = self.substr(0, self.find_last_of(L'\\'));

        wchar_t temp[MAX_PATH + 1]{};
        GetTempPathW(MAX_PATH + 1, temp);
        std::wstring copy = std::wstring(temp) + L"Rave-Uninstall-" + std::to_wstring(GetTickCount64()) + L".exe";
        if (!CopyFileW(self.c_str(), copy.c_str(), FALSE))
            return runUninstaller(dir);

        std::wstring cmd = L"\"" + copy + L"\" /uninstall-from \"" + dir + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, temp, &si, &pi))
        {
            DeleteFileW(copy.c_str());
            return runUninstaller(dir);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    instance = hInstance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    appIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    appIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_RAVE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring self = selfPath();
    std::wstring name = self.substr(self.find_last_of(L'\\') + 1);

    int result;
    if (argc >= 3 && _wcsicmp(argv[1], L"/uninstall-from") == 0)
    {
        initUi();
        result = runUninstaller(argv[2]);
        deleteSelfSoon(self);
    }
    else if ((argc >= 2 && _wcsicmp(argv[1], L"/uninstall") == 0) || _wcsicmp(name.c_str(), L"uninstall.exe") == 0)
    {
        result = startUninstall();
    }
    else
    {
        initUi();
        result = runInstaller();
    }

    LocalFree(argv);
    CoUninitialize();
    return result;
}
