#include "protocol.hpp"
#include "json.hpp"
#include "paths.hpp"
#include "system.hpp"
#include <windows.h>
#include <shlobj.h>
#include <vector>

namespace
{
    const wchar_t* kClasses = L"Software\\Classes";
    const wchar_t* kKey = L"Software\\Classes\\roblox-player";
    const wchar_t* kScheme = L"roblox-player";

    struct Entry
    {
        std::wstring key;
        std::wstring name;
        DWORD type = REG_SZ;
        std::vector<BYTE> data;
    };

    std::wstring backupPath()
    {
        return paths::localData() + L"\\protocol.json";
    }

    std::wstring command()
    {
        return L"\"" + sys::exePath() + L"\" \"%1\"";
    }

    std::wstring readDefault(const std::wstring& sub)
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};
        wchar_t buf[2048]{};
        DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
        std::wstring out;
        if (RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE*)buf, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
            out = buf;
        RegCloseKey(key);
        return out;
    }

    void collect(HKEY root, const std::wstring& path, const std::wstring& rel, std::vector<Entry>& out)
    {
        HKEY key;
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return;

        for (DWORD i = 0;; ++i)
        {
            wchar_t name[16384];
            DWORD nameLen = 16384, type = 0, size = 0;
            LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, nullptr, &size);
            if (r != ERROR_SUCCESS)
                break;
            Entry e;
            e.key = rel;
            e.name.assign(name, nameLen);
            e.type = type;
            e.data.resize(size);
            nameLen = 16384;
            if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, e.data.data(), &size) == ERROR_SUCCESS)
            {
                e.data.resize(size);
                out.push_back(std::move(e));
            }
        }

        std::vector<std::wstring> subs;
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256];
            DWORD nameLen = 256;
            if (RegEnumKeyExW(key, i, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            subs.emplace_back(name, nameLen);
        }
        RegCloseKey(key);

        for (const std::wstring& s : subs)
        {
            Entry marker;
            marker.key = rel.empty() ? s : rel + L"\\" + s;
            marker.type = REG_NONE;
            marker.name = L"\x1";
            out.push_back(marker);
            collect(root, path + L"\\" + s, marker.key, out);
        }
    }

    std::string hex(const std::vector<BYTE>& data)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (BYTE b : data)
        {
            out += digits[b >> 4];
            out += digits[b & 15];
        }
        return out;
    }

    std::vector<BYTE> unhex(const std::string& text)
    {
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        std::vector<BYTE> out;
        for (size_t i = 0; i + 1 < text.size(); i += 2)
        {
            int hi = val(text[i]), lo = val(text[i + 1]);
            if (hi < 0 || lo < 0)
                return {};
            out.push_back((BYTE)(hi * 16 + lo));
        }
        return out;
    }

    bool writeFile(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
        CloseHandle(h);
        return ok;
    }

    bool readFile(const std::wstring& path, std::string& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20);
        if (ok)
        {
            out.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
        }
        CloseHandle(h);
        return ok;
    }

    bool saveBackup()
    {
        HKEY probe;
        bool existed = RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &probe) == ERROR_SUCCESS;
        if (existed)
            RegCloseKey(probe);

        std::vector<Entry> entries;
        if (existed)
            collect(HKEY_CURRENT_USER, kKey, L"", entries);

        std::string text = "{\r\n    \"existed\": ";
        text += existed ? "true" : "false";
        text += ",\r\n    \"values\": [";
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const Entry& e = entries[i];
            text += i ? ",\r\n        " : "\r\n        ";
            text += "{ \"key\": " + json::quote(sys::narrow(e.key)) +
                ", \"name\": " + json::quote(e.name == L"\x1" ? std::string("\x1") : sys::narrow(e.name)) +
                ", \"type\": " + std::to_string(e.type) +
                ", \"data\": \"" + hex(e.data) + "\" }";
        }
        text += entries.empty() ? "]\r\n}\r\n" : "\r\n    ]\r\n}\r\n";
        return writeFile(backupPath(), text);
    }

    bool setString(const std::wstring& sub, const wchar_t* name, const std::wstring& value)
    {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        LONG r = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return r == ERROR_SUCCESS;
    }

    void notifyShell()
    {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
}

bool protocol::isLaunch(const std::wstring& arg)
{
    return arg.size() > 14 && arg.size() < 8192 && _wcsnicmp(arg.c_str(), L"roblox-player:", 14) == 0 &&
        arg.find_first_of(L"\"\r\n") == std::wstring::npos;
}

bool protocol::registered()
{
    std::wstring cmd = readDefault(std::wstring(kKey) + L"\\shell\\open\\command");
    return !cmd.empty() && _wcsicmp(cmd.c_str(), command().c_str()) == 0;
}

bool protocol::enable(std::string& error)
{
    if (registered())
        return true;

    if (!saveBackup())
    {
        error = "could not save the current roblox-player handler";
        return false;
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, kKey);
    std::wstring exe = sys::exePath();
    bool ok = setString(kKey, nullptr, L"URL: Roblox Protocol") &&
        setString(kKey, L"URL Protocol", L"") &&
        setString(std::wstring(kKey) + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",-2") &&
        setString(std::wstring(kKey) + L"\\shell\\open\\command", nullptr, command());
    notifyShell();
    if (!ok)
    {
        std::string ignored;
        disable(ignored);
        error = "could not write the roblox-player handler";
        return false;
    }
    return true;
}

bool protocol::disable(std::string& error)
{
    std::string text;
    json::Value v;
    bool haveBackup = readFile(backupPath(), text) && json::parse(text, v) && v.isObject();

    if (!haveBackup && !registered())
        return true;

    HKEY classes;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kClasses, 0, KEY_ALL_ACCESS, &classes) == ERROR_SUCCESS)
    {
        RegDeleteTreeW(classes, kScheme);
        RegDeleteKeyW(classes, kScheme);
        RegCloseKey(classes);
    }

    bool ok = true;
    if (haveBackup && v["existed"].type == json::Value::Type::Bool && v["existed"].boolean)
    {
        HKEY root;
        ok = RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) == ERROR_SUCCESS;
        if (ok)
            RegCloseKey(root);
        const json::Value& values = v["values"];
        for (size_t i = 0; ok && i < values.items.size(); ++i)
        {
            const json::Value& e = values[i];
            std::wstring sub = sys::widen(e["key"].str());
            std::wstring path = sub.empty() ? std::wstring(kKey) : std::wstring(kKey) + L"\\" + sub;
            HKEY key;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            {
                ok = false;
                break;
            }
            if (e["name"].str() != "\x1")
            {
                std::wstring name = sys::widen(e["name"].str());
                std::vector<BYTE> data = unhex(e["data"].str());
                if (RegSetValueExW(key, name.empty() ? nullptr : name.c_str(), 0, (DWORD)e["type"].number, data.empty() ? nullptr : data.data(), (DWORD)data.size()) != ERROR_SUCCESS)
                    ok = false;
            }
            RegCloseKey(key);
        }
    }
    notifyShell();

    if (!ok)
    {
        error = "could not restore the old roblox-player handler";
        return false;
    }
    DeleteFileW(backupPath().c_str());
    return true;
}

