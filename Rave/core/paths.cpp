#include "paths.hpp"
#include <windows.h>
#include <shlobj.h>

bool paths::exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool paths::ensureDir(const std::wstring& dir)
{
    if (dir.empty())
        return false;
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 2)
        ensureDir(dir.substr(0, slash));

    if (CreateDirectoryW(dir.c_str(), nullptr))
        return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool paths::removeTree(const std::wstring& dir)
{
    bool ok = true;
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
            {
                ok &= removeTree(full);
            }
            else
            {
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    ok &= RemoveDirectoryW(full.c_str()) != 0;
                else
                    ok &= DeleteFileW(full.c_str()) != 0;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryW(dir.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        ok = false;
    return ok;
}

std::wstring paths::localData()
{
    std::wstring dir;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)))
        dir = raw;
    CoTaskMemFree(raw);
    if (dir.empty())
    {
        wchar_t env[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", env, MAX_PATH);
        dir = env;
    }
    dir += L"\\Rave";
    ensureDir(dir);
    return dir;
}

std::wstring paths::versions()
{
    std::wstring dir = localData() + L"\\Versions";
    ensureDir(dir);
    return dir;
}

std::wstring paths::tempDir()
{
    wchar_t tmp[MAX_PATH + 1]{};
    DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    std::wstring dir(tmp, n);
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
        dir.pop_back();
    dir += L"\\rave-temp";
    ensureDir(dir);
    return dir;
}
