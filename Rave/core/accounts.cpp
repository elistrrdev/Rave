#include "accounts.hpp"
#include "json.hpp"
#include "net.hpp"
#include "paths.hpp"
#include "system.hpp"
#include <windows.h>
#include <dpapi.h>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace
{
    struct Stored
    {
        accounts::Account info;
        std::string cookie;
    };

    std::mutex lock;
    std::vector<Stored> stored;
    uint64_t selectedId = 0;
    std::atomic<unsigned> added{ 0 };
    launcher::Logger logger;

    std::mutex quickLock;
    accounts::Quick quickState;
    std::string qrData;
    std::atomic<unsigned> quickGen{ 0 };

    std::mutex pasteLock;
    accounts::Paste pasteState;

    std::mutex avatarLock;
    std::map<uint64_t, std::string> avatars;

    const char kEntropy[] = "Rave accounts v1";
    const wchar_t* kCookieName = L".ROBLOSECURITY";

    void log(const std::string& text, launcher::LogLevel level)
    {
        if (logger)
            logger(text, level);
    }

    std::wstring filePath()
    {
        return paths::localData() + L"\\accounts.dat";
    }

    uint64_t parseId(const json::Value& v)
    {
        if (v.type == json::Value::Type::Number && v.number > 0)
            return (uint64_t)v.number;
        if (v.isString())
            return _strtoui64(v.str().c_str(), nullptr, 10);
        return 0;
    }

    bool protect(const std::string& plain, std::string& out)
    {
        DATA_BLOB in{ (DWORD)plain.size(), (BYTE*)plain.data() };
        DATA_BLOB entropy{ (DWORD)(sizeof(kEntropy) - 1), (BYTE*)kEntropy };
        DATA_BLOB result{};
        if (!CryptProtectData(&in, L"Rave", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result))
            return false;
        out.assign((const char*)result.pbData, result.cbData);
        LocalFree(result.pbData);
        return true;
    }

    bool unprotect(const std::string& blob, std::string& out)
    {
        DATA_BLOB in{ (DWORD)blob.size(), (BYTE*)blob.data() };
        DATA_BLOB entropy{ (DWORD)(sizeof(kEntropy) - 1), (BYTE*)kEntropy };
        DATA_BLOB result{};
        if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result))
            return false;
        out.assign((const char*)result.pbData, result.cbData);
        SecureZeroMemory(result.pbData, result.cbData);
        LocalFree(result.pbData);
        return true;
    }

    bool readFile(const std::wstring& path, std::string& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20);
        if (ok)
        {
            out.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
        }
        CloseHandle(h);
        return ok;
    }

    bool writeFile(const std::wstring& path, const std::string& data)
    {
        std::wstring temp = path + L".tmp";
        HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
        FlushFileBuffers(h);
        CloseHandle(h);
        if (ok)
            ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok)
            DeleteFileW(temp.c_str());
        return ok;
    }

    void saveLocked()
    {
        std::string text = "{\"selected\":" + json::quote(std::to_string(selectedId)) + ",\"accounts\":[";
        for (size_t i = 0; i < stored.size(); ++i)
        {
            const Stored& s = stored[i];
            if (i)
                text += ",";
            text += "{\"id\":" + json::quote(std::to_string(s.info.id));
            text += ",\"name\":" + json::quote(s.info.name);
            text += ",\"displayName\":" + json::quote(s.info.displayName);
            text += std::string(",\"quick\":") + (s.info.quick ? "true" : "false");
            text += std::string(",\"expired\":") + (s.info.expired ? "true" : "false");
            text += ",\"cookie\":" + json::quote(s.cookie) + "}";
        }
        text += "]}";

        std::string blob;
        bool ok = protect(text, blob) && writeFile(filePath(), blob);
        SecureZeroMemory(text.data(), text.size());
        if (!ok)
            log("Could not save accounts.dat", launcher::LogLevel::Error);
    }

    Stored* findLocked(uint64_t id)
    {
        for (auto& s : stored)
            if (s.info.id == id)
                return &s;
        return nullptr;
    }

    struct Call
    {
        std::wstring method = L"GET";
        std::wstring url;
        std::string body;
        std::string cookie;
    };

    bool rbx(const Call& call, net::Response& r, std::string& csrf)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            net::Request req;
            req.method = call.method;
            req.url = call.url;
            req.body = call.body;
            std::wstring h = L"Accept: application/json\r\nReferer: https://www.roblox.com/\r\n";
            if (call.method != L"GET")
                h += L"Content-Type: application/json\r\n";
            if (!csrf.empty())
                h += L"x-csrf-token: " + sys::widen(csrf) + L"\r\n";
            if (!call.cookie.empty())
                h += L"Cookie: " + std::wstring(kCookieName) + L"=" + sys::widen(call.cookie) + L"\r\n";
            req.headers = h;
            if (!net::send(req, r))
                return false;
            std::string token = r.header("x-csrf-token");
            if (r.status == 403 && !token.empty() && token != csrf)
            {
                csrf = token;
                continue;
            }
            return true;
        }
        return true;
    }

    bool safeHeaderValue(const std::string& v)
    {
        for (unsigned char c : v)
            if (c < 0x21 || c > 0x7e || c == ';' || c == ',')
                return false;
        return !v.empty();
    }

    void rotate(uint64_t id, const net::Response& r)
    {
        std::string fresh = r.cookie(".ROBLOSECURITY");
        if (!safeHeaderValue(fresh))
            return;
        std::lock_guard<std::mutex> guard(lock);
        Stored* s = findLocked(id);
        if (s && s->cookie != fresh)
        {
            s->cookie = fresh;
            saveLocked();
        }
    }

    enum class UserResult { Ok, Invalid, Failed };

    UserResult fetchUser(const std::string& cookie, accounts::Account& out, std::string* rotated)
    {
        std::string csrf;
        net::Response r;
        if (!rbx({ L"GET", L"https://users.roblox.com/v1/users/authenticated", "", cookie }, r, csrf))
            return UserResult::Failed;
        if (r.status == 401)
            return UserResult::Invalid;
        json::Value v;
        if (!r.ok() || !json::parse(r.body, v) || !v.isObject())
            return UserResult::Failed;
        out.id = parseId(v["id"]);
        out.name = v["name"].str();
        out.displayName = v["displayName"].str();
        if (!out.id || out.name.empty())
            return UserResult::Failed;
        if (rotated)
        {
            std::string fresh = r.cookie(".ROBLOSECURITY");
            if (safeHeaderValue(fresh))
                *rotated = fresh;
        }
        return UserResult::Ok;
    }

    void logoutAsync(const std::string& cookie)
    {
        if (cookie.empty())
            return;
        std::thread([cookie] {
            std::string csrf;
            net::Response r;
            rbx({ L"POST", L"https://auth.roblox.com/v2/logout", "", cookie }, r, csrf);
        }).detach();
    }

    bool rbxcdnUrl(const std::string& url)
    {
        const std::string scheme = "https://";
        if (url.rfind(scheme, 0) != 0)
            return false;
        size_t slash = url.find('/', scheme.size());
        std::string host = url.substr(scheme.size(), slash == std::string::npos ? std::string::npos : slash - scheme.size());
        const std::string suffix = ".rbxcdn.com";
        return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void fetchAvatars(const std::vector<uint64_t>& ids)
    {
        if (ids.empty())
            return;
        std::wstring url = L"https://thumbnails.roblox.com/v1/users/avatar-headshot?size=150x150&format=Png&isCircular=false&userIds=";
        for (size_t i = 0; i < ids.size(); ++i)
            url += (i ? L"," : L"") + std::to_wstring(ids[i]);

        net::Response r;
        json::Value v;
        if (!net::get(url, r) || !r.ok() || !json::parse(r.body, v))
            return;
        const json::Value& data = v["data"];
        for (size_t i = 0; i < data.items.size(); ++i)
        {
            const json::Value& item = data[i];
            uint64_t id = parseId(item["targetId"]);
            const std::string& image = item["imageUrl"].str();
            if (!id || item["state"].str() != "Completed" || !rbxcdnUrl(image))
                continue;
            net::Response img;
            if (!net::get(sys::widen(image), img) || !img.ok() || img.body.empty() || img.body.size() > (1 << 20))
                continue;
            std::lock_guard<std::mutex> guard(avatarLock);
            avatars[id] = std::move(img.body);
        }
    }

    void storeAccount(accounts::Account info, const std::string& cookie, bool& updated)
    {
        std::string oldQuickCookie;
        {
            std::lock_guard<std::mutex> guard(lock);
            info.expired = false;
            Stored* s = findLocked(info.id);
            updated = s != nullptr;
            if (s)
            {
                if (s->info.quick && s->cookie != cookie)
                    oldQuickCookie = s->cookie;
                s->info = info;
                s->cookie = cookie;
            }
            else
            {
                stored.push_back({ info, cookie });
            }
            selectedId = info.id;
            saveLocked();
        }
        logoutAsync(oldQuickCookie);
        ++added;
    }

    void refreshAll()
    {
        std::vector<std::pair<uint64_t, std::string>> list;
        {
            std::lock_guard<std::mutex> guard(lock);
            for (auto& s : stored)
                list.push_back({ s.info.id, s.cookie });
        }

        std::vector<uint64_t> ids;
        for (auto& [id, cookie] : list)
        {
            ids.push_back(id);
            accounts::Account info;
            std::string rotated;
            UserResult res = fetchUser(cookie, info, &rotated);
            if (res == UserResult::Failed)
                continue;

            std::lock_guard<std::mutex> guard(lock);
            Stored* s = findLocked(id);
            if (!s || s->cookie != cookie)
                continue;
            if (res == UserResult::Invalid)
            {
                if (!s->info.expired)
                {
                    s->info.expired = true;
                    saveLocked();
                }
                continue;
            }
            if (info.id != id)
                continue;
            bool changed = s->info.expired || s->info.name != info.name || s->info.displayName != info.displayName;
            s->info.expired = false;
            s->info.name = info.name;
            s->info.displayName = info.displayName;
            if (!rotated.empty() && rotated != s->cookie)
            {
                s->cookie = rotated;
                changed = true;
            }
            if (changed)
                saveLocked();
        }
        fetchAvatars(ids);
    }

    bool quickAlive(unsigned gen)
    {
        return quickGen.load() == gen;
    }

    template<typename Fn>
    void updateQuick(unsigned gen, Fn fn)
    {
        std::lock_guard<std::mutex> guard(quickLock);
        if (quickAlive(gen))
            fn(quickState);
    }

    void quickFail(unsigned gen, const std::string& message)
    {
        updateQuick(gen, [&](accounts::Quick& q) {
            q.state = accounts::QuickState::Failed;
            q.message = message;
        });
    }

    bool waitAlive(unsigned gen, DWORD ms)
    {
        for (DWORD waited = 0; waited < ms; waited += 100)
        {
            if (!quickAlive(gen))
                return false;
            Sleep(100);
        }
        return quickAlive(gen);
    }

    void runQuick(unsigned gen)
    {
        std::string csrf;
        for (int round = 0; round < 5; ++round)
        {
            if (!quickAlive(gen))
                return;
            updateQuick(gen, [](accounts::Quick& q) {
                q.state = accounts::QuickState::Starting;
                q.code.clear();
                q.linkedName.clear();
                q.message = "Getting a code from Roblox...";
            });

            net::Response r;
            json::Value v;
            if (!rbx({ L"POST", L"https://apis.roblox.com/auth-token-service/v1/login/create", "{}", "" }, r, csrf) || !r.ok() || !json::parse(r.body, v))
            {
                quickFail(gen, r.status ? "Roblox did not give a code (" + std::to_string(r.status) + ")" : "Roblox did not answer");
                return;
            }
            std::string code = v["code"].str();
            std::string key = v["privateKey"].str();
            if (code.empty() || key.empty())
            {
                quickFail(gen, "Roblox did not give a code");
                return;
            }

            std::string png;
            net::Response qr;
            std::wstring qrUrl = L"https://apis.roblox.com/auth-token-service/v1/login/qr-code-image?key=" + sys::widen(key) + L"&code=" + sys::widen(code);
            if (net::get(qrUrl, qr) && qr.ok() && qr.body.size() < (1 << 16))
                png = qr.body;

            {
                std::lock_guard<std::mutex> guard(quickLock);
                if (!quickAlive(gen))
                    return;
                qrData = png;
                quickState.state = accounts::QuickState::Waiting;
                quickState.code = code;
                quickState.linkedName.clear();
                quickState.message = "Waiting for the code to be entered...";
                ++quickState.qrVersion;
            }

            std::string body = "{\"code\":" + json::quote(code) + ",\"privateKey\":" + json::quote(key) + "}";
            ULONGLONG started = GetTickCount64();
            bool validated = false;
            bool expired = false;
            while (!validated && !expired)
            {
                if (!waitAlive(gen, 2500))
                    return;
                if (GetTickCount64() - started > 115000)
                {
                    expired = true;
                    break;
                }
                net::Response s;
                json::Value sv;
                if (!rbx({ L"POST", L"https://apis.roblox.com/auth-token-service/v1/login/status", body, "" }, s, csrf))
                    continue;
                if (s.status == 400 || s.status == 404)
                {
                    expired = true;
                    break;
                }
                if (!s.ok() || !json::parse(s.body, sv))
                    continue;
                const std::string& status = sv["status"].str();
                if (status == "UserLinked")
                {
                    std::string who = sv["accountName"].str();
                    updateQuick(gen, [&](accounts::Quick& q) {
                        q.state = accounts::QuickState::Linked;
                        q.linkedName = who;
                        q.message = who.empty() ? "Confirm the login on your other device" : "Confirm the login for @" + who + " on your other device";
                    });
                }
                else if (status == "Validated")
                {
                    validated = true;
                }
                else if (status == "Cancelled")
                {
                    quickFail(gen, "The login was declined on the other device");
                    return;
                }
            }
            if (expired)
                continue;

            updateQuick(gen, [](accounts::Quick& q) {
                q.state = accounts::QuickState::SigningIn;
                q.message = "Signing in...";
            });

            net::Response l;
            std::string loginBody = "{\"ctype\":\"AuthToken\",\"cvalue\":" + json::quote(code) + ",\"password\":" + json::quote(key) + "}";
            if (!rbx({ L"POST", L"https://auth.roblox.com/v2/login", loginBody, "" }, l, csrf))
            {
                quickFail(gen, "Roblox did not answer");
                return;
            }
            std::string cookie = l.cookie(".ROBLOSECURITY");
            if (!l.ok() || !safeHeaderValue(cookie))
            {
                if (!l.header("rblx-challenge-id").empty())
                    quickFail(gen, "Roblox wants an extra security check. Try again later or use COOKIE.");
                else
                    quickFail(gen, "Roblox did not accept the login (" + std::to_string(l.status) + ")");
                return;
            }

            accounts::Account info;
            std::string rotated;
            if (fetchUser(cookie, info, &rotated) != UserResult::Ok)
            {
                logoutAsync(cookie);
                quickFail(gen, "Signed in, but the account could not be read");
                return;
            }
            if (!rotated.empty())
                cookie = rotated;
            if (!quickAlive(gen))
            {
                logoutAsync(cookie);
                return;
            }

            info.quick = true;
            bool updated = false;
            storeAccount(info, cookie, updated);
            std::string name = accounts::label(info);
            updateQuick(gen, [&](accounts::Quick& q) {
                q.state = accounts::QuickState::Done;
                q.message = "Signed in as " + name;
            });
            log((updated ? "Account updated: " : "Account added: ") + name, launcher::LogLevel::Success);
            fetchAvatars({ info.id });
            return;
        }
        quickFail(gen, "The code expired. Click NEW CODE to get a new one.");
    }

    std::string cleanCookie(std::string c)
    {
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n\"'");
            size_t b = s.find_last_not_of(" \t\r\n\"';");
            s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        trim(c);
        const std::string prefix = ".ROBLOSECURITY=";
        if (c.rfind(prefix, 0) == 0)
            c = c.substr(prefix.size());
        trim(c);
        return safeHeaderValue(c) ? c : std::string();
    }
}

void accounts::setLogger(launcher::Logger l)
{
    logger = std::move(l);
}

void accounts::load()
{
    std::string blob, plain;
    json::Value v;
    if (readFile(filePath(), blob) && unprotect(blob, plain) && json::parse(plain, v) && v.isObject())
    {
        std::lock_guard<std::mutex> guard(lock);
        stored.clear();
        const json::Value& list = v["accounts"];
        for (size_t i = 0; i < list.items.size(); ++i)
        {
            const json::Value& a = list[i];
            Stored s;
            s.info.id = parseId(a["id"]);
            s.info.name = a["name"].str();
            s.info.displayName = a["displayName"].str();
            s.info.quick = a["quick"].boolean;
            s.info.expired = a["expired"].boolean;
            s.cookie = a["cookie"].str();
            if (s.info.id && !s.info.name.empty() && safeHeaderValue(s.cookie) && !findLocked(s.info.id))
                stored.push_back(std::move(s));
        }
        uint64_t sel = parseId(v["selected"]);
        selectedId = findLocked(sel) ? sel : 0;
    }
    SecureZeroMemory(plain.data(), plain.size());

    std::thread(refreshAll).detach();
}

std::vector<accounts::Account> accounts::list()
{
    std::lock_guard<std::mutex> guard(lock);
    std::vector<Account> out;
    for (auto& s : stored)
        out.push_back(s.info);
    return out;
}

uint64_t accounts::selected()
{
    std::lock_guard<std::mutex> guard(lock);
    return selectedId;
}

void accounts::select(uint64_t id)
{
    std::lock_guard<std::mutex> guard(lock);
    if (id != 0 && !findLocked(id))
        return;
    if (selectedId == id)
        return;
    selectedId = id;
    saveLocked();
}

void accounts::remove(uint64_t id)
{
    Stored removed;
    bool found = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto it = stored.begin(); it != stored.end(); ++it)
        {
            if (it->info.id != id)
                continue;
            removed = *it;
            stored.erase(it);
            found = true;
            break;
        }
        if (!found)
            return;
        if (selectedId == id)
            selectedId = 0;
        saveLocked();
    }
    {
        std::lock_guard<std::mutex> guard(avatarLock);
        avatars.erase(id);
    }
    if (removed.info.quick)
        logoutAsync(removed.cookie);
    log("Account removed: " + label(removed.info), launcher::LogLevel::Info);
}

unsigned accounts::addedCount()
{
    return added.load();
}

std::string accounts::label(const Account& a)
{
    if (a.displayName.empty() || a.displayName == a.name)
        return "@" + a.name;
    return a.displayName + " (@" + a.name + ")";
}

void accounts::startQuick()
{
    unsigned gen = ++quickGen;
    {
        std::lock_guard<std::mutex> guard(quickLock);
        quickState.state = QuickState::Starting;
        quickState.code.clear();
        quickState.linkedName.clear();
        quickState.message = "Getting a code from Roblox...";
        qrData.clear();
        ++quickState.qrVersion;
    }
    std::thread(runQuick, gen).detach();
}

void accounts::cancelQuick()
{
    ++quickGen;
    std::lock_guard<std::mutex> guard(quickLock);
    quickState.state = QuickState::Idle;
    quickState.code.clear();
    quickState.linkedName.clear();
    quickState.message.clear();
    qrData.clear();
    ++quickState.qrVersion;
}

accounts::Quick accounts::quick()
{
    std::lock_guard<std::mutex> guard(quickLock);
    return quickState;
}

std::string accounts::qrPng()
{
    std::lock_guard<std::mutex> guard(quickLock);
    return qrData;
}

void accounts::addCookie(const std::string& raw)
{
    std::string cookie = cleanCookie(raw);
    {
        std::lock_guard<std::mutex> guard(pasteLock);
        if (pasteState.state == PasteState::Checking)
            return;
        if (cookie.empty())
        {
            pasteState = { PasteState::Failed, "That does not look like a .ROBLOSECURITY cookie" };
            return;
        }
        pasteState = { PasteState::Checking, "Checking the cookie..." };
    }

    std::thread([cookie] {
        Account info;
        std::string rotated;
        UserResult res = fetchUser(cookie, info, &rotated);
        if (res != UserResult::Ok)
        {
            std::lock_guard<std::mutex> guard(pasteLock);
            pasteState = { PasteState::Failed, res == UserResult::Invalid ? "This cookie is not valid or has expired" : "Roblox did not answer, please try again" };
            return;
        }
        bool updated = false;
        storeAccount(info, rotated.empty() ? cookie : rotated, updated);
        std::string name = label(info);
        {
            std::lock_guard<std::mutex> guard(pasteLock);
            pasteState = { PasteState::Done, (updated ? "Updated " : "Added ") + name };
        }
        log((updated ? "Account updated: " : "Account added: ") + name, launcher::LogLevel::Success);
        fetchAvatars({ info.id });
    }).detach();
}

accounts::Paste accounts::paste()
{
    std::lock_guard<std::mutex> guard(pasteLock);
    return pasteState;
}

void accounts::resetPaste()
{
    std::lock_guard<std::mutex> guard(pasteLock);
    if (pasteState.state != PasteState::Checking)
        pasteState = {};
}

bool accounts::avatarPng(uint64_t id, std::string& png)
{
    std::lock_guard<std::mutex> guard(avatarLock);
    auto it = avatars.find(id);
    if (it == avatars.end())
        return false;
    png = it->second;
    return true;
}

bool accounts::ticket(uint64_t id, std::string& ticket, Account& who, std::string& error)
{
    std::string cookie;
    {
        std::lock_guard<std::mutex> guard(lock);
        Stored* s = findLocked(id);
        if (!s)
        {
            error = "the account is not in Rave anymore";
            return false;
        }
        who = s->info;
        cookie = s->cookie;
    }

    std::string csrf;
    net::Response r;
    if (!rbx({ L"POST", L"https://auth.roblox.com/v1/authentication-ticket", "", cookie }, r, csrf))
    {
        error = "Roblox did not answer";
        return false;
    }
    if (r.status == 401)
    {
        std::lock_guard<std::mutex> guard(lock);
        Stored* s = findLocked(id);
        if (s && !s->info.expired)
        {
            s->info.expired = true;
            saveLocked();
        }
        error = "the login of " + label(who) + " has expired, add the account again";
        return false;
    }
    ticket = r.header("rbx-authentication-ticket");
    bool safe = !ticket.empty();
    for (unsigned char c : ticket)
        if (c < 0x21 || c > 0x7e || c == '"')
            safe = false;
    if (!r.ok() || !safe)
    {
        ticket.clear();
        error = "Roblox did not give a sign-in ticket (" + std::to_string(r.status) + ")";
        return false;
    }
    rotate(id, r);
    return true;
}
