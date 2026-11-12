#include "roblox.hpp"
#include "json.hpp"
#include "net.hpp"
#include "paths.hpp"
#include "system.hpp"
#include "zip.hpp"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
    const wchar_t* kVersionUrl = L"https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer";
    const wchar_t* kSetupUrl = L"https://setup.rbxcdn.com/";
    const wchar_t* kPlayerExe = L"RobloxPlayerBeta.exe";

    struct Package
    {
        std::string name;
        std::string md5;
        uint64_t packed = 0;
        uint64_t unpacked = 0;
    };

    struct Mapping
    {
        const char* name;
        const wchar_t* dir;
    };

    const Mapping kMappings[] = {
        { "RobloxApp.zip", L"" },
        { "redist.zip", L"" },
        { "shaders.zip", L"shaders" },
        { "ssl.zip", L"ssl" },
        { "WebView2.zip", L"" },
        { "WebView2RuntimeInstaller.zip", L"WebView2RuntimeInstaller" },
        { "content-avatar.zip", L"content\\avatar" },
        { "content-configs.zip", L"content\\configs" },
        { "content-fonts.zip", L"content\\fonts" },
        { "content-sky.zip", L"content\\sky" },
        { "content-sounds.zip", L"content\\sounds" },
        { "content-textures2.zip", L"content\\textures" },
        { "content-models.zip", L"content\\models" },
        { "content-platform-fonts.zip", L"PlatformContent\\pc\\fonts" },
        { "content-platform-dictionaries.zip", L"PlatformContent\\pc\\shared_compression_dictionaries" },
        { "content-terrain.zip", L"PlatformContent\\pc\\terrain" },
        { "content-textures3.zip", L"PlatformContent\\pc\\textures" },
        { "extracontent-luapackages.zip", L"ExtraContent\\LuaPackages" },
        { "extracontent-translations.zip", L"ExtraContent\\translations" },
        { "extracontent-models.zip", L"ExtraContent\\models" },
        { "extracontent-textures.zip", L"ExtraContent\\textures" },
        { "extracontent-places.zip", L"ExtraContent\\places" },
    };

    const char* kAppSettings =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
        "<Settings>\r\n"
        "\t<ContentFolder>content</ContentFolder>\r\n"
        "\t<BaseUrl>http://www.roblox.com</BaseUrl>\r\n"
        "</Settings>\r\n";

    std::wstring targetFor(const std::string& name)
    {
        for (const auto& m : kMappings)
            if (_stricmp(m.name, name.c_str()) == 0)
                return m.dir;
        return L"";
    }

    bool parseManifest(const std::string& text, std::vector<Package>& out)
    {
        std::istringstream in(text);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty())
                lines.push_back(line);
        }
        if (lines.empty() || lines[0] != "v0")
            return false;
        for (size_t i = 1; i + 3 < lines.size(); i += 4)
        {
            Package p;
            p.name = lines[i];
            p.md5 = lines[i + 1];
            p.packed = _strtoui64(lines[i + 2].c_str(), nullptr, 10);
            p.unpacked = _strtoui64(lines[i + 3].c_str(), nullptr, 10);
            out.push_back(p);
        }
        return !out.empty();
    }

    bool writeText(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
        CloseHandle(h);
        return ok;
    }

    bool isComplete(const std::wstring& dir)
    {
        return paths::exists(dir + L"\\AppSettings.xml") && paths::exists(dir + L"\\" + kPlayerExe);
    }
}

bool roblox::fetchLatest(Latest& out, std::string& error)
{
    net::Response r;
    if (!net::get(kVersionUrl, r) || !r.ok())
    {
        error = "could not reach the Roblox version service";
        return false;
    }
    json::Value v;
    if (!json::parse(r.body, v) || !v["clientVersionUpload"].isString())
    {
        error = "unexpected answer from the Roblox version service";
        return false;
    }
    out.guid = v["clientVersionUpload"].str();
    out.version = v["version"].str();
    if (out.guid.rfind("version-", 0) != 0)
    {
        error = "invalid Roblox version";
        return false;
    }
    return true;
}

std::wstring roblox::versionDir(const std::string& guid)
{
    return paths::versions() + L"\\" + sys::widen(guid);
}

std::string roblox::installedGuid()
{
    std::string best;
    FILETIME bestTime{};
    std::wstring root = paths::versions();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\version-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return best;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        std::wstring dir = root + L"\\" + fd.cFileName;
        if (!isComplete(dir))
            continue;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        GetFileAttributesExW((dir + L"\\AppSettings.xml").c_str(), GetFileExInfoStandard, &data);
        if (best.empty() || CompareFileTime(&data.ftLastWriteTime, &bestTime) > 0)
        {
            best = sys::narrow(fd.cFileName);
            bestTime = data.ftLastWriteTime;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

bool roblox::install(const std::string& guid, const Progress& progress, std::string& error)
{
    std::wstring wguid = sys::widen(guid);
    net::Response manifest;
    if (!net::get(kSetupUrl + wguid + L"-rbxPkgManifest.txt", manifest) || !manifest.ok())
    {
        error = "could not download the Roblox package list";
        return false;
    }

    std::vector<Package> all, packages;
    if (!parseManifest(manifest.body, all))
    {
        error = "could not read the Roblox package list";
        return false;
    }
    uint64_t totalBytes = 0;
    for (auto& p : all)
    {
        if (p.name.size() > 4 && _stricmp(p.name.c_str() + p.name.size() - 4, ".zip") == 0)
        {
            packages.push_back(p);
            totalBytes += p.packed;
        }
    }
    if (packages.empty() || totalBytes == 0)
    {
        error = "the Roblox package list is empty";
        return false;
    }

    std::wstring dir = versionDir(guid);
    if (paths::exists(dir))
        paths::removeTree(dir);
    if (!paths::ensureDir(dir))
    {
        error = "could not create the Roblox folder";
        return false;
    }

    std::atomic<uint64_t> downloaded{ 0 };
    std::atomic<uint64_t> unpacked{ 0 };
    std::atomic<size_t> next{ 0 };
    std::atomic<bool> failed{ false };
    std::mutex errorLock;
    std::string firstError;

    auto fail = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(errorLock);
        if (!failed.exchange(true))
            firstError = msg;
    };

    std::atomic<unsigned> finished{ 0 };
    auto worker = [&] {
        struct Done
        {
            std::atomic<unsigned>& n;
            ~Done() { ++n; }
        } done{ finished };

        for (;;)
        {
            if (failed)
                return;
            size_t index = next++;
            if (index >= packages.size())
                return;
            const Package& pkg = packages[index];

            uint64_t last = 0;
            net::Response r;
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok && !failed; ++attempt)
            {
                downloaded -= last;
                last = 0;
                ok = net::get(kSetupUrl + wguid + L"-" + sys::widen(pkg.name), r, [&](uint64_t done, uint64_t) {
                    downloaded += done - last;
                    last = done;
                    return !failed.load();
                }, L"", 30000) && r.ok() && r.body.size() == pkg.packed && sys::md5Hex(r.body.data(), r.body.size()) == pkg.md5;
            }
            if (!ok)
            {
                fail("download failed for " + pkg.name);
                return;
            }

            std::wstring target = dir;
            std::wstring sub = targetFor(pkg.name);
            if (!sub.empty())
                target += L"\\" + sub;

            std::string zipError;
            if (!zip::extract(r.body, target, zipError))
            {
                fail("could not unpack " + pkg.name + " (" + zipError + ")");
                return;
            }
            unpacked += pkg.packed;
        }
    };

    unsigned threads = std::max(2u, std::min(4u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < threads; ++i)
        pool.emplace_back(worker);

    bool aborted = false;
    while (true)
    {
        bool alive = finished.load() < threads;
        float fraction = (float)((double)(downloaded.load() + unpacked.load()) / (2.0 * (double)totalBytes));
        if (progress && !progress(std::min(fraction, 1.f)) && !aborted)
        {
            aborted = true;
            fail("installation cancelled");
        }
        if (!alive)
            break;
        Sleep(100);
    }
    for (auto& t : pool)
        t.join();

    if (failed)
    {
        error = firstError;
        paths::removeTree(dir);
        return false;
    }

    if (!writeText(dir + L"\\AppSettings.xml", kAppSettings) || !isComplete(dir))
    {
        error = "the Roblox installation is incomplete";
        paths::removeTree(dir);
        return false;
    }
    if (progress)
        progress(1.f);
    return true;
}

void roblox::removeOldVersions(const std::string& keepGuid)
{
    std::wstring root = paths::versions();
    std::wstring keep = sys::widen(keepGuid);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\version-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::wstring> remove;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && _wcsicmp(fd.cFileName, keep.c_str()) != 0)
            remove.push_back(root + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& dir : remove)
        paths::removeTree(dir);
}

bool roblox::launch(const std::string& guid, std::string& error)
{
    std::wstring dir = versionDir(guid);
    if (!isComplete(dir))
    {
        error = "Roblox is not installed";
        return false;
    }
    if (!sys::launchUnelevated(dir + L"\\" + kPlayerExe, L"--app", dir))
    {
        error = "could not start Roblox";
        return false;
    }
    return true;
}

bool roblox::isRunning()
{
    return !sys::findProcesses(kPlayerExe).empty();
}
