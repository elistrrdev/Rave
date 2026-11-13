#include "updater.hpp"
#include "json.hpp"
#include "launcher.hpp"
#include "net.hpp"
#include "paths.hpp"
#include "system.hpp"
#include "../version.hpp"
#include <shellapi.h>
#include <thread>
#include <vector>

namespace
{
    const wchar_t* kReleases = L"https://api.github.com/repos/elistrrdev/Rave/releases?per_page=20";
    const wchar_t* kApiHeaders = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    const char* kAsset = "Rave-Installer.exe";

    struct Version
    {
        int major = 0, minor = 0, patch = 0;

        bool newerThan(const Version& o) const
        {
            if (major != o.major) return major > o.major;
            if (minor != o.minor) return minor > o.minor;
            return patch > o.patch;
        }
    };

    bool parseVersion(std::string tag, Version& out)
    {
        if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
            tag.erase(0, 1);
        int parts[3] = { 0, 0, 0 };
        int count = 0;
        size_t pos = 0;
        while (count < 3)
        {
            size_t end = pos;
            while (end < tag.size() && tag[end] >= '0' && tag[end] <= '9')
                ++end;
            if (end == pos || end - pos > 6)
                return false;
            parts[count++] = std::stoi(tag.substr(pos, end - pos));
            if (end == tag.size())
                break;
            if (tag[end] != '.')
                return false;
            pos = end + 1;
        }
        if (count < 2)
            return false;
        out = { parts[0], parts[1], parts[2] };
        return true;
    }

    struct Update
    {
        std::string tag;
        Version version;
        std::wstring url;
        uint64_t size = 0;
        std::string sha256;
    };

    std::wstring folderOf(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    }

    std::wstring trimSlashes(std::wstring dir)
    {
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
            dir.pop_back();
        return dir;
    }

    bool runsInstalled()
    {
        wchar_t value[MAX_PATH * 2]{};
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Rave", L"InstallPath", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value, &size) != ERROR_SUCCESS)
            return false;
        std::wstring installed = trimSlashes(value);
        std::wstring running = trimSlashes(folderOf(sys::exePath()));
        return !installed.empty() && _wcsicmp(installed.c_str(), running.c_str()) == 0;
    }

    bool pickUpdate(const std::string& body, const Version& current, Update& out)
    {
        json::Value list;
        if (!json::parse(body, list) || list.type != json::Value::Type::Array)
            return false;

        bool found = false;
        for (const json::Value& release : list.items)
        {
            if (release["draft"].boolean)
                continue;
            Version v;
            if (!release["tag_name"].isString() || !parseVersion(release["tag_name"].str(), v) || !v.newerThan(current))
                continue;
            if (found && !v.newerThan(out.version))
                continue;
            for (const json::Value& asset : release["assets"].items)
            {
                if (asset["name"].str() != kAsset)
                    continue;
                std::string url = asset["browser_download_url"].str();
                if (url.rfind("https://github.com/elistrrdev/Rave/releases/download/", 0) != 0)
                    continue;
                out.tag = release["tag_name"].str();
                out.version = v;
                out.url = sys::widen(url);
                out.size = (uint64_t)asset["size"].number;
                std::string digest = asset["digest"].str();
                out.sha256 = digest.rfind("sha256:", 0) == 0 ? digest.substr(7) : std::string();
                found = true;
                break;
            }
        }
        return found;
    }

    bool findUpdate(Update& out)
    {
        net::Response res;
        if (!net::get(kReleases, res, nullptr, kApiHeaders) || !res.ok())
            return false;
        return pickUpdate(res.body, { RAVE_VERSION_MAJOR, RAVE_VERSION_MINOR, 0 }, out);
    }

    void waitUntilIdle()
    {
        while (launcher::task() != launcher::Task::Idle)
            Sleep(1000);
    }

    bool writeFile(const std::wstring& path, const std::string& data)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
        CloseHandle(h);
        if (!ok)
            DeleteFileW(path.c_str());
        return ok;
    }

    bool download(const Update& update, std::wstring& path)
    {
        net::Response res;
        if (!net::get(update.url, res, nullptr, L"", 60000) || !res.ok())
            return false;
        if (res.body.size() < 2 || res.body[0] != 'M' || res.body[1] != 'Z')
            return false;
        if (update.size && res.body.size() != update.size)
            return false;
        if (!update.sha256.empty() && _stricmp(sys::sha256Hex(res.body.data(), res.body.size()).c_str(), update.sha256.c_str()) != 0)
            return false;
        path = paths::tempDir() + L"\\Rave-Update-" + sys::widen(update.tag) + L".exe";
        return writeFile(path, res.body);
    }

    bool startSetup(const std::wstring& path)
    {
        std::wstring args = L"/update " + std::to_wstring(GetCurrentProcessId());
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"runas";
        info.lpFile = path.c_str();
        info.lpParameters = args.c_str();
        info.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&info) != FALSE;
    }

    void run(HWND window)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (!runsInstalled())
            return;
        Update update;
        if (!findUpdate(update))
            return;
        waitUntilIdle();
        std::wstring path;
        if (!download(update, path))
            return;
        waitUntilIdle();
        if (!startSetup(path))
        {
            DeleteFileW(path.c_str());
            return;
        }
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
}

void updater::start(HWND window)
{
    std::thread([window] { run(window); }).detach();
}
