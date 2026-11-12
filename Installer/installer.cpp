#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shldisp.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <string>
#include <thread>
#include <vector>
#include "resource.h"
#include "../Rave/version.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
    const wchar_t* kTitle = L"Rave V" RAVE_VERSION_WSTR L" Setup";
    const wchar_t* kUninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Rave";
    const wchar_t* kAppKey = L"SOFTWARE\\Rave";
    const UINT WM_INSTALL_STATUS = WM_APP + 1;
    const UINT WM_INSTALL_PROGRESS = WM_APP + 2;
    const UINT WM_INSTALL_DONE = WM_APP + 3;

    HINSTANCE instance;
    HFONT titleFont;
    HICON appIcon;
    HICON appIconSmall;
    HWND sheet;

    std::wstring installDir;
    bool startMenu = true;
    bool desktop = true;
    bool launchAfter = true;
    bool installing = false;
    bool installed = false;

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

    std::wstring regRead(const wchar_t* key, const wchar_t* value)
    {
        wchar_t buf[MAX_PATH * 2]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) == ERROR_SUCCESS)
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
            int answer = MessageBoxW(owner, L"Rave is currently running.\n\nClick OK to close Rave and continue, or Cancel to stop.", kTitle, MB_OKCANCEL | MB_ICONWARNING);
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
        std::wstring existing = regRead(kAppKey, L"InstallPath");
        if (!existing.empty())
            return existing;
        return knownFolder(FOLDERID_ProgramFiles) + L"\\Rave";
    }

    HBITMAP makeBitmap(int w, int h, bool watermark)
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

        if (watermark)
        {
            for (int y = 0; y < h; ++y)
            {
                float t = (float)y / (float)h;
                int r = (int)(14 + (20 - 14) * t);
                int g = (int)(20 + (44 - 20) * t);
                int b = (int)(19 + (38 - 19) * t);
                HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
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
        }
        else
        {
            RECT all{ 0, 0, w, h };
            HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(dc, &all, brush);
            DeleteObject(brush);
            HICON icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON, w, h, 0);
            if (icon)
            {
                DrawIconEx(dc, 0, 0, icon, w, h, 0, nullptr, DI_NORMAL);
                DestroyIcon(icon);
            }
        }

        SelectObject(dc, old);
        DeleteDC(dc);
        return bmp;
    }

    void setResult(HWND dlg, LONG_PTR value)
    {
        SetWindowLongPtrW(dlg, DWLP_MSGRESULT, value);
    }

    bool confirmCancel(HWND dlg)
    {
        if (installing)
            return false;
        if (installed)
            return true;
        return MessageBoxW(dlg, L"Are you sure you want to quit Rave Setup?", kTitle, MB_YESNO | MB_ICONQUESTION) == IDYES;
    }

    INT_PTR CALLBACK welcomeProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            SendDlgItemMessageW(dlg, IDC_TITLE, WM_SETFONT, (WPARAM)titleFont, TRUE);
            SetDlgItemTextW(dlg, IDC_TITLE, L"Welcome to Rave V" RAVE_VERSION_WSTR L" Setup");
            SetDlgItemTextW(dlg, IDC_TEXT,
                L"This wizard will guide you through the installation of Rave V" RAVE_VERSION_WSTR L".\r\n\r\n"
                L"Rave is a Roblox launcher that always downloads and starts the latest Roblox version. "
                L"Its built-in anticheat follows with Rave V0.2.\r\n\r\n"
                L"It is recommended that you close all other applications before starting Setup.\r\n\r\n"
                L"Click Next to continue.");
            return TRUE;
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_NEXT);
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_QUERYCANCEL)
            {
                setResult(dlg, confirmCancel(dlg) ? FALSE : TRUE);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    void updateLicenseButtons(HWND dlg)
    {
        bool accepted = IsDlgButtonChecked(dlg, IDC_ACCEPT) == BST_CHECKED;
        PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_BACK | (accepted ? PSWIZB_NEXT : 0));
    }

    INT_PTR CALLBACK licenseProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            Resource lic = loadResource(IDR_LICENSE);
            std::string raw((const char*)lic.data, lic.size);
            std::string text;
            text.reserve(raw.size() + raw.size() / 20);
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                    text += '\r';
                text += raw[i];
            }
            int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
            std::wstring wide(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), n);
            SendDlgItemMessageW(dlg, IDC_LICENSE_TEXT, EM_SETLIMITTEXT, 0, 0);
            SetDlgItemTextW(dlg, IDC_LICENSE_TEXT, wide.c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_ACCEPT && HIWORD(wp) == BN_CLICKED)
                updateLicenseButtons(dlg);
            break;
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                updateLicenseButtons(dlg);
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_QUERYCANCEL)
            {
                setResult(dlg, confirmCancel(dlg) ? FALSE : TRUE);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    void updateSpace(HWND dlg)
    {
        Resource payload = loadResource(IDR_PAYLOAD);
        ULONGLONG required = (ULONGLONG)payload.size * 2;
        SetDlgItemTextW(dlg, IDC_SPACE_REQUIRED, (L"Space required: " + formatSize(required)).c_str());

        wchar_t path[MAX_PATH * 2]{};
        GetDlgItemTextW(dlg, IDC_PATH, path, MAX_PATH * 2);
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
        SetDlgItemTextW(dlg, IDC_SPACE_AVAILABLE, found ? (L"Space available: " + formatSize(freeBytes.QuadPart)).c_str() : L"Space available: unknown");
    }

    void browse(HWND dlg)
    {
        ComPtr<IFileOpenDialog> picker;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
            return;
        DWORD options = 0;
        picker->GetOptions(&options);
        picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        picker->SetTitle(L"Select the folder to install Rave in");
        if (FAILED(picker->Show(GetParent(dlg))))
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
        SetDlgItemTextW(dlg, IDC_PATH, chosen.c_str());
    }

    INT_PTR CALLBACK locationProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            SetDlgItemTextW(dlg, IDC_PATH, defaultDir().c_str());
            updateSpace(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_BROWSE && HIWORD(wp) == BN_CLICKED)
                browse(dlg);
            if (LOWORD(wp) == IDC_PATH && HIWORD(wp) == EN_CHANGE)
                updateSpace(dlg);
            break;
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_BACK | PSWIZB_NEXT);
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_WIZNEXT)
            {
                wchar_t path[MAX_PATH * 2]{};
                GetDlgItemTextW(dlg, IDC_PATH, path, MAX_PATH * 2);
                std::wstring dir = path;
                while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L' '))
                    dir.pop_back();
                if (dir.size() < 3 || PathIsRelativeW(dir.c_str()) || dir.find_first_of(L"*?\"<>|") != std::wstring::npos || PathIsRootW(dir.c_str()))
                {
                    MessageBoxW(dlg, L"Please choose a valid folder to install Rave in.", kTitle, MB_OK | MB_ICONWARNING);
                    setResult(dlg, -1);
                    return TRUE;
                }
                installDir = dir;
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_QUERYCANCEL)
            {
                setResult(dlg, confirmCancel(dlg) ? FALSE : TRUE);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    INT_PTR CALLBACK optionsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            CheckDlgButton(dlg, IDC_STARTMENU, BST_CHECKED);
            CheckDlgButton(dlg, IDC_DESKTOP, BST_CHECKED);
            return TRUE;
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_BACK | PSWIZB_NEXT);
                PropSheet_SetButtonText(GetParent(dlg), PSWIZB_NEXT, L"&Install");
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_KILLACTIVE)
            {
                PropSheet_SetButtonText(GetParent(dlg), PSWIZB_NEXT, L"&Next >");
                break;
            }
            if (n->code == PSN_WIZNEXT)
            {
                startMenu = IsDlgButtonChecked(dlg, IDC_STARTMENU) == BST_CHECKED;
                desktop = IsDlgButtonChecked(dlg, IDC_DESKTOP) == BST_CHECKED;
                PropSheet_SetButtonText(GetParent(dlg), PSWIZB_NEXT, L"&Next >");
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_QUERYCANCEL)
            {
                setResult(dlg, confirmCancel(dlg) ? FALSE : TRUE);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    void post(HWND dlg, UINT msg, const std::wstring& text, WPARAM value = 0)
    {
        std::wstring* copy = new std::wstring(text);
        if (!PostMessageW(dlg, msg, value, (LPARAM)copy))
            delete copy;
    }

    void installWorker(HWND dlg)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto status = [&](const std::wstring& text, int percent) {
            post(dlg, WM_INSTALL_STATUS, text, (WPARAM)percent);
        };
        auto fail = [&](const std::wstring& text) {
            post(dlg, WM_INSTALL_DONE, text, 0);
            CoUninitialize();
        };

        status(L"Checking for a running Rave...", 5);
        if (!closeRave(GetParent(dlg)))
        {
            fail(L"Setup was cancelled because Rave is still running.");
            return;
        }

        status(L"Create folder: " + installDir, 15);
        if (!ensureDir(installDir))
        {
            fail(L"Could not create the folder:\r\n" + installDir);
            return;
        }

        Resource payload = loadResource(IDR_PAYLOAD);
        std::wstring exe = installDir + L"\\Rave.exe";
        status(L"Extract: Rave.exe", 35);
        if (!payload.data || !writeFile(exe, payload.data, payload.size))
        {
            fail(L"Could not write:\r\n" + exe);
            return;
        }

        std::wstring uninstaller = installDir + L"\\uninstall.exe";
        status(L"Create uninstaller: uninstall.exe", 60);
        if (!CopyFileW(selfPath().c_str(), uninstaller.c_str(), FALSE))
        {
            fail(L"Could not write:\r\n" + uninstaller);
            return;
        }

        status(L"Create shortcuts", 75);
        if (startMenu)
            createShortcut(startMenuShortcut(), exe, installDir);
        else
            DeleteFileW(startMenuShortcut().c_str());
        if (desktop)
            createShortcut(desktopShortcut(), exe, installDir);
        else
            DeleteFileW(desktopShortcut().c_str());

        status(L"Register Rave in Apps and Features", 90);
        HKEY key;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            regWrite(key, L"DisplayName", L"Rave");
            regWrite(key, L"DisplayVersion", RAVE_VERSION_WSTR);
            regWrite(key, L"Publisher", RAVE_PUBLISHER);
            regWrite(key, L"DisplayIcon", exe);
            regWrite(key, L"InstallLocation", installDir);
            regWrite(key, L"UninstallString", L"\"" + uninstaller + L"\"");
            regWrite(key, L"QuietUninstallString", L"\"" + uninstaller + L"\"");
            regWrite(key, L"URLInfoAbout", RAVE_REPO_URL);
            regWrite(key, L"HelpLink", RAVE_REPO_URL);
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
        status(L"Completed", 100);
        post(dlg, WM_INSTALL_DONE, L"", 1);
        CoUninitialize();
    }

    INT_PTR CALLBACK installProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            SendDlgItemMessageW(dlg, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            return TRUE;
        case WM_INSTALL_STATUS:
        {
            std::wstring* text = (std::wstring*)lp;
            SetDlgItemTextW(dlg, IDC_STATUS, text->c_str());
            LRESULT index = SendDlgItemMessageW(dlg, IDC_DETAILS, LB_ADDSTRING, 0, (LPARAM)text->c_str());
            SendDlgItemMessageW(dlg, IDC_DETAILS, LB_SETTOPINDEX, (WPARAM)index, 0);
            SendDlgItemMessageW(dlg, IDC_PROGRESS, PBM_SETPOS, wp, 0);
            delete text;
            return TRUE;
        }
        case WM_INSTALL_DONE:
        {
            std::wstring* text = (std::wstring*)lp;
            installing = false;
            if (wp)
            {
                installed = true;
                PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_NEXT);
                PropSheet_PressButton(GetParent(dlg), PSBTN_NEXT);
            }
            else
            {
                SetDlgItemTextW(dlg, IDC_STATUS, L"Installation failed");
                SendDlgItemMessageW(dlg, IDC_DETAILS, LB_ADDSTRING, 0, (LPARAM)text->c_str());
                EnableWindow(GetDlgItem(GetParent(dlg), IDCANCEL), TRUE);
                PropSheet_SetWizButtons(GetParent(dlg), 0);
                MessageBoxW(dlg, text->c_str(), kTitle, MB_OK | MB_ICONERROR);
            }
            delete text;
            return TRUE;
        }
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                if (installed)
                {
                    setResult(dlg, -1);
                    return TRUE;
                }
                PropSheet_SetWizButtons(GetParent(dlg), 0);
                EnableWindow(GetDlgItem(GetParent(dlg), IDCANCEL), FALSE);
                installing = true;
                std::thread(installWorker, dlg).detach();
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_WIZBACK)
            {
                setResult(dlg, -1);
                return TRUE;
            }
            if (n->code == PSN_QUERYCANCEL)
            {
                setResult(dlg, confirmCancel(dlg) ? FALSE : TRUE);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    INT_PTR CALLBACK finishProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            SendDlgItemMessageW(dlg, IDC_TITLE, WM_SETFONT, (WPARAM)titleFont, TRUE);
            SetDlgItemTextW(dlg, IDC_TITLE, L"Completing Rave V" RAVE_VERSION_WSTR L" Setup");
            SetDlgItemTextW(dlg, IDC_TEXT, L"Rave V" RAVE_VERSION_WSTR L" has been installed on your computer.\r\n\r\nClick Finish to close Setup.");
            SetDlgItemTextW(dlg, IDC_LAUNCH, L"&Launch Rave V" RAVE_VERSION_WSTR);
            CheckDlgButton(dlg, IDC_LAUNCH, BST_CHECKED);
            return TRUE;
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            if (n->code == PSN_SETACTIVE)
            {
                PropSheet_SetWizButtons(GetParent(dlg), PSWIZB_FINISH);
                EnableWindow(GetDlgItem(GetParent(dlg), IDCANCEL), FALSE);
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_WIZFINISH)
            {
                launchAfter = IsDlgButtonChecked(dlg, IDC_LAUNCH) == BST_CHECKED;
                setResult(dlg, 0);
                return TRUE;
            }
            if (n->code == PSN_WIZBACK)
            {
                setResult(dlg, -1);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    int CALLBACK sheetCallback(HWND hwnd, UINT msg, LPARAM)
    {
        if (msg == PSCB_INITIALIZED)
        {
            sheet = hwnd;
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIconSmall);
        }
        return 0;
    }

    PROPSHEETPAGEW page(int id, DLGPROC proc, bool exterior, const wchar_t* title, const wchar_t* subtitle)
    {
        PROPSHEETPAGEW p{};
        p.dwSize = sizeof(p);
        p.hInstance = instance;
        p.pszTemplate = MAKEINTRESOURCEW(id);
        p.pfnDlgProc = proc;
        if (exterior)
            p.dwFlags = PSP_HIDEHEADER;
        else
        {
            p.dwFlags = PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
            p.pszHeaderTitle = title;
            p.pszHeaderSubTitle = subtitle;
        }
        return p;
    }

    int runInstaller()
    {
        UINT dpi = 96;
        HDC screen = GetDC(nullptr);
        dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);

        titleFont = CreateFontW(-MulDiv(12, (int)dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HBITMAP watermark = makeBitmap(MulDiv(164, (int)dpi, 96), MulDiv(314, (int)dpi, 96), true);
        HBITMAP header = makeBitmap(MulDiv(49, (int)dpi, 96), MulDiv(49, (int)dpi, 96), false);

        PROPSHEETPAGEW pages[] = {
            page(IDD_WELCOME, welcomeProc, true, nullptr, nullptr),
            page(IDD_LICENSE, licenseProc, false, L"License Agreement", L"Please review the license terms before installing Rave V" RAVE_VERSION_WSTR L"."),
            page(IDD_LOCATION, locationProc, false, L"Choose Install Location", L"Choose the folder in which to install Rave V" RAVE_VERSION_WSTR L"."),
            page(IDD_OPTIONS, optionsProc, false, L"Choose Additional Tasks", L"Which additional tasks should be performed?"),
            page(IDD_INSTALL, installProc, false, L"Installing", L"Please wait while Rave V" RAVE_VERSION_WSTR L" is being installed."),
            page(IDD_FINISH, finishProc, true, nullptr, nullptr),
        };

        PROPSHEETHEADERW h{};
        h.dwSize = sizeof(h);
        h.dwFlags = PSH_WIZARD97 | PSH_PROPSHEETPAGE | PSH_WATERMARK | PSH_HEADER | PSH_USEHBMWATERMARK | PSH_USEHBMHEADER | PSH_USECALLBACK;
        h.hInstance = instance;
        h.hwndParent = nullptr;
        h.pszCaption = kTitle;
        h.nPages = (UINT)(sizeof(pages) / sizeof(pages[0]));
        h.ppsp = pages;
        h.hbmWatermark = watermark;
        h.hbmHeader = header;
        h.pfnCallback = sheetCallback;

        PropertySheetW(&h);

        if (installed && launchAfter)
            launchRave();

        DeleteObject(watermark);
        DeleteObject(header);
        DeleteObject(titleFont);
        return installed ? 0 : 1;
    }

    int runUninstaller(const std::wstring& dir)
    {
        std::wstring exe = dir + L"\\Rave.exe";
        if (MessageBoxW(nullptr, L"Are you sure you want to completely remove Rave and all of its components?", L"Rave Uninstall", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 1;

        if (!closeRave(nullptr))
            return 1;

        deleteFileRetry(exe);
        deleteFileRetry(dir + L"\\uninstall.exe");
        RemoveDirectoryW(dir.c_str());

        DeleteFileW(startMenuShortcut().c_str());
        DeleteFileW(desktopShortcut().c_str());

        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kAppKey, KEY_WOW64_64KEY, 0);

        std::wstring data = knownFolder(FOLDERID_LocalAppData) + L"\\Rave";
        if (GetFileAttributesW(data.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring question = L"Do you also want to remove the Rave user data?\n\nThis deletes the Roblox versions Rave downloaded in:\n" + data;
            if (MessageBoxW(nullptr, question.c_str(), L"Rave Uninstall", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                removeTree(data);
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        MessageBoxW(nullptr, L"Rave was successfully removed from your computer.", L"Rave Uninstall", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    int startUninstall()
    {
        std::wstring self = selfPath();
        std::wstring dir = regRead(kAppKey, L"InstallPath");
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

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    appIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    appIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring self = selfPath();
    std::wstring name = self.substr(self.find_last_of(L'\\') + 1);

    int result;
    if (argc >= 3 && _wcsicmp(argv[1], L"/uninstall-from") == 0)
    {
        result = runUninstaller(argv[2]);
        MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    else if ((argc >= 2 && _wcsicmp(argv[1], L"/uninstall") == 0) || _wcsicmp(name.c_str(), L"uninstall.exe") == 0)
    {
        result = startUninstall();
    }
    else
    {
        result = runInstaller();
    }

    LocalFree(argv);
    CoUninitialize();
    return result;
}
