#include "fflags.hpp"
#include "json.hpp"
#include "paths.hpp"
#include "roblox.hpp"
#include <windows.h>

namespace
{
    std::wstring storePath()
    {
        return paths::localData() + L"\\fflags.json";
    }

    bool readText(const std::wstring& path, std::string& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (1 << 24);
        if (ok)
        {
            out.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
        }
        CloseHandle(h);
        return ok;
    }

    bool writeText(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = text.empty() || (WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size());
        CloseHandle(h);
        return ok;
    }

    bool isBlank(const std::string& text)
    {
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
    }
}

std::string fflags::load()
{
    std::string text;
    if (!readText(storePath(), text))
        return {};
    return text;
}

bool fflags::validate(const std::string& text, std::string& error)
{
    if (isBlank(text))
        return true;
    json::Value v;
    if (!json::parse(text, v))
    {
        error = "This is not valid JSON";
        return false;
    }
    if (!v.isObject())
    {
        error = "FFlags must be a JSON object like { \"Flag\": value }";
        return false;
    }
    return true;
}

bool fflags::save(const std::string& text, std::string& error)
{
    if (!validate(text, error))
        return false;
    if (!writeText(storePath(), text))
    {
        error = "Could not write fflags.json";
        return false;
    }
    std::string installed = roblox::installedGuid();
    if (!installed.empty() && !apply(roblox::versionDir(installed)))
    {
        error = "Saved, but could not write ClientAppSettings.json";
        return false;
    }
    return true;
}

bool fflags::apply(const std::wstring& versionDir)
{
    std::string text = load();
    std::string error;
    if (isBlank(text) || !validate(text, error))
        text = "{}";
    std::wstring dir = versionDir + L"\\ClientSettings";
    if (!paths::ensureDir(dir))
        return false;
    return writeText(dir + L"\\ClientAppSettings.json", text);
}
