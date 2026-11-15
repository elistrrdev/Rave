#include "discord.hpp"
#include "json.hpp"
#include "net.hpp"
#include "roblox.hpp"
#include "settings.hpp"
#include "system.hpp"
#include <windows.h>
#include <shlobj.h>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
    const char* kAppId = "1479505409331953827";

    struct GameInfo
    {
        std::string name;
        std::string creator;
        std::string icon;
    };

    struct Session
    {
        uint64_t place = 0;
        uint64_t universe = 0;
        int64_t start = 0;
    };

    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD lastConnectTry = 0;
    int nonce = 0;

    void closePipe()
    {
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }

    bool writeFrame(uint32_t op, const std::string& body)
    {
        if (pipe == INVALID_HANDLE_VALUE)
            return false;
        std::string frame(8, '\0');
        uint32_t len = (uint32_t)body.size();
        memcpy(&frame[0], &op, 4);
        memcpy(&frame[4], &len, 4);
        frame += body;
        DWORD written = 0;
        if (!WriteFile(pipe, frame.data(), (DWORD)frame.size(), &written, nullptr) || written != frame.size())
        {
            closePipe();
            return false;
        }
        return true;
    }

    bool readFrame(uint32_t& op, std::string& body, DWORD waitMs)
    {
        DWORD start = GetTickCount();
        for (;;)
        {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr))
            {
                closePipe();
                return false;
            }
            if (avail >= 8)
                break;
            if (GetTickCount() - start > waitMs)
                return false;
            Sleep(20);
        }
        uint32_t header[2]{};
        DWORD read = 0;
        if (!ReadFile(pipe, header, 8, &read, nullptr) || read != 8 || header[1] > (1u << 20))
        {
            closePipe();
            return false;
        }
        op = header[0];
        body.assign(header[1], '\0');
        size_t got = 0;
        while (got < body.size())
        {
            if (!ReadFile(pipe, &body[got], (DWORD)(body.size() - got), &read, nullptr) || read == 0)
            {
                closePipe();
                return false;
            }
            got += read;
        }
        return true;
    }

    void drain()
    {
        uint32_t op;
        std::string body;
        while (pipe != INVALID_HANDLE_VALUE && readFrame(op, body, 0))
        {
            if (op == 3)
                writeFrame(4, body);
            else if (op == 2)
                closePipe();
        }
    }

    bool connect()
    {
        if (pipe != INVALID_HANDLE_VALUE)
            return true;
        if (GetTickCount() - lastConnectTry < 10000)
            return false;
        lastConnectTry = GetTickCount();

        for (int i = 0; i < 10 && pipe == INVALID_HANDLE_VALUE; ++i)
        {
            std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (pipe == INVALID_HANDLE_VALUE)
            return false;

        if (!writeFrame(0, std::string("{\"v\":1,\"client_id\":") + json::quote(kAppId) + "}"))
            return false;
        uint32_t op = 0;
        std::string body;
        if (!readFrame(op, body, 3000) || op != 1)
        {
            closePipe();
            return false;
        }
        return true;
    }

    bool sendActivity(const std::string& activity)
    {
        if (!connect())
            return false;
        drain();
        std::string body = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
            ",\"activity\":" + activity + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
        if (!writeFrame(1, body))
            return false;
        uint32_t op;
        std::string reply;
        readFrame(op, reply, 2000);
        return pipe != INVALID_HANDLE_VALUE;
    }

    std::map<uint64_t, GameInfo> infoCache;

    GameInfo gameInfo(uint64_t universe)
    {
        auto it = infoCache.find(universe);
        if (it != infoCache.end())
            return it->second;

        GameInfo info;
        net::Response r;
        json::Value v;
        if (net::get(L"https://games.roblox.com/v1/games?universeIds=" + std::to_wstring(universe), r) && r.ok() && json::parse(r.body, v))
        {
            info.name = v["data"][0]["name"].str();
            info.creator = v["data"][0]["creator"]["name"].str();
        }
        net::Response t;
        json::Value tv;
        if (net::get(L"https://thumbnails.roblox.com/v1/games/icons?universeIds=" + std::to_wstring(universe) +
            L"&returnPolicy=PlaceHolder&size=512x512&format=Png&isCircular=false", t) && t.ok() && json::parse(t.body, tv))
        {
            std::string url = tv["data"][0]["imageUrl"].str();
            if (url.rfind("https://", 0) == 0)
                info.icon = url;
        }
        if (!info.name.empty())
            infoCache[universe] = info;
        return info;
    }

    std::string activityFor(const Session& s)
    {
        GameInfo info = gameInfo(s.universe);
        std::string name = info.name.empty() ? "Roblox" : info.name;
        if (name.size() > 120)
            name.resize(120);
        std::string a = "{\"type\":0,\"details\":" + json::quote(name);
        if (!info.creator.empty())
            a += ",\"state\":" + json::quote(("by " + info.creator).substr(0, 120));
        a += ",\"timestamps\":{\"start\":" + std::to_string(s.start) + "}";
        a += ",\"assets\":{";
        if (!info.icon.empty())
            a += "\"large_image\":" + json::quote(info.icon) + ",";
        a += "\"large_text\":" + json::quote(name) + "}";
        a += ",\"buttons\":[{\"label\":\"Open the game page\",\"url\":" +
            json::quote("https://www.roblox.com/games/" + std::to_string(s.place)) + "}]";
        a += ",\"instance\":false}";
        return a;
    }

    std::wstring newestLog()
    {
        wchar_t* base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
            return {};
        std::wstring dir = std::wstring(base) + L"\\Roblox\\logs";
        CoTaskMemFree(base);

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*_Player_*_last.log").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return {};
        std::wstring best;
        FILETIME bestTime{};
        do
        {
            std::wstring name = fd.cFileName;
            if (name.find(L"CrashHandler") != std::wstring::npos)
                continue;
            if (best.empty() || CompareFileTime(&fd.ftLastWriteTime, &bestTime) > 0)
            {
                best = name;
                bestTime = fd.ftLastWriteTime;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return best.empty() ? std::wstring() : dir + L"\\" + best;
    }

    int64_t lineTime(const std::string& line)
    {
        if (line.size() < 19 || line[4] != '-' || line[10] != 'T')
            return (int64_t)time(nullptr);
        std::tm t{};
        t.tm_year = atoi(line.substr(0, 4).c_str()) - 1900;
        t.tm_mon = atoi(line.substr(5, 2).c_str()) - 1;
        t.tm_mday = atoi(line.substr(8, 2).c_str());
        t.tm_hour = atoi(line.substr(11, 2).c_str());
        t.tm_min = atoi(line.substr(14, 2).c_str());
        t.tm_sec = atoi(line.substr(17, 2).c_str());
        int64_t v = (int64_t)_mkgmtime(&t);
        return v > 0 ? v : (int64_t)time(nullptr);
    }

    uint64_t numberAfter(const std::string& line, const std::string& key)
    {
        size_t at = line.find(key);
        if (at == std::string::npos)
            return 0;
        return _strtoui64(line.c_str() + at + key.size(), nullptr, 10);
    }

    class Watcher
    {
    public:
        void reset()
        {
            file.clear();
            offset = 0;
            partial.clear();
            current = {};
            pendingPlace = 0;
            pendingStart = 0;
            leftUniverse = 0;
            leftStart = 0;
            leftAt = 0;
        }

        bool poll()
        {
            std::wstring log = newestLog();
            if (log.empty())
                return false;
            if (log != file)
            {
                reset();
                file = log;
            }

            HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return false;
            LARGE_INTEGER size{};
            GetFileSizeEx(h, &size);
            if ((uint64_t)size.QuadPart < offset)
            {
                offset = 0;
                partial.clear();
            }
            bool changed = false;
            while ((uint64_t)size.QuadPart > offset)
            {
                LARGE_INTEGER pos;
                pos.QuadPart = (LONGLONG)offset;
                SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
                std::string chunk((size_t)std::min<uint64_t>(size.QuadPart - offset, 1 << 20), '\0');
                DWORD read = 0;
                if (!ReadFile(h, chunk.data(), (DWORD)chunk.size(), &read, nullptr) || read == 0)
                    break;
                chunk.resize(read);
                offset += read;
                partial += chunk;
                size_t start = 0, nl;
                while ((nl = partial.find('\n', start)) != std::string::npos)
                {
                    changed |= line(partial.substr(start, nl - start));
                    start = nl + 1;
                }
                partial.erase(0, start);
                if (partial.size() > 65536)
                    partial.clear();
            }
            CloseHandle(h);
            return changed;
        }

        Session current;

    private:
        bool line(const std::string& text)
        {
            if (text.find("[FLog::Output] ! Joining game '") != std::string::npos)
            {
                pendingPlace = numberAfter(text, "' place ");
                pendingStart = lineTime(text);
                return false;
            }
            if (text.find("[FLog::GameJoinLoadTime] Report game_join_loadtime:") != std::string::npos)
            {
                uint64_t universe = numberAfter(text, "universeid:");
                uint64_t place = numberAfter(text, "placeid:");
                if (!universe)
                    return false;
                Session s;
                s.place = place ? place : pendingPlace;
                s.universe = universe;
                s.start = pendingStart ? pendingStart : lineTime(text);
                if (universe == leftUniverse && s.start - leftAt < 60 && leftStart)
                    s.start = leftStart;
                bool changed = s.universe != current.universe || s.place != current.place || s.start != current.start;
                current = s;
                return changed;
            }
            if (text.find("[FLog::Network] Time to disconnect replication data:") != std::string::npos ||
                text.find("[FLog::SingleSurfaceApp] leaveUGCGameInternal") != std::string::npos)
            {
                if (!current.universe)
                    return false;
                leftUniverse = current.universe;
                leftStart = current.start;
                leftAt = lineTime(text);
                current = {};
                return true;
            }
            return false;
        }

        std::wstring file;
        uint64_t offset = 0;
        std::string partial;
        uint64_t pendingPlace = 0;
        int64_t pendingStart = 0;
        uint64_t leftUniverse = 0;
        int64_t leftStart = 0;
        int64_t leftAt = 0;
    };

    void run()
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Watcher watcher;
        bool shown = false;
        bool dirty = false;
        bool wasRunning = false;
        for (;;)
        {
            Sleep(2000);
            bool enabled = settings::get().discordStatus && discord::configured();
            bool running = roblox::isRunning();

            if (!running)
            {
                if (wasRunning)
                    watcher.reset();
                wasRunning = false;
                if (shown)
                    sendActivity("null");
                shown = false;
                closePipe();
                continue;
            }
            if (!wasRunning)
            {
                watcher.reset();
                dirty = true;
            }
            wasRunning = true;

            if (watcher.poll())
                dirty = true;

            if (!enabled)
            {
                if (shown && sendActivity("null"))
                    shown = false;
                if (!shown)
                    closePipe();
                dirty = true;
                continue;
            }

            if (!dirty)
            {
                if (pipe != INVALID_HANDLE_VALUE)
                    drain();
                continue;
            }

            if (watcher.current.universe)
            {
                if (sendActivity(activityFor(watcher.current)))
                {
                    shown = true;
                    dirty = false;
                }
            }
            else if (shown)
            {
                if (sendActivity("null"))
                {
                    shown = false;
                    dirty = false;
                }
            }
            else
                dirty = false;
        }
    }
}

bool discord::configured()
{
    return kAppId[0] != '\0';
}

void discord::start()
{
    std::thread(run).detach();
}
