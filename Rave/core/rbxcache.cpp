#include "rbxcache.hpp"
#include "paths.hpp"
#include "system.hpp"
#include "__ext/sqlite/sqlite3.h"
#include <windows.h>
#include <cstring>

namespace
{
    const uint32_t kSeed = 0x00C4E564;
    const size_t kInlineLimit = 7000;
    const int kContentCategory = 10;

    uint32_t rd32(const uint8_t* p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    void wr32(uint8_t* p, uint32_t v)
    {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }

    uint32_t rotl(uint32_t x, int r)
    {
        return (x << r) | (x >> (32 - r));
    }

    std::string toHex(const void* data, int len)
    {
        static const char* digits = "0123456789abcdef";
        const uint8_t* b = (const uint8_t*)data;
        std::string out;
        for (int i = 0; i < len; ++i)
        {
            out += digits[b[i] >> 4];
            out += digits[b[i] & 15];
        }
        return out;
    }

    bool fromHex(const std::string& hex, uint8_t out[16])
    {
        if (hex.size() != 32)
            return false;
        for (int i = 0; i < 16; ++i)
        {
            int v = 0;
            for (int k = 0; k < 2; ++k)
            {
                char c = hex[i * 2 + k];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= c - '0';
                else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                else return false;
            }
            out[i] = (uint8_t)v;
        }
        return true;
    }

    bool parseHeader(const uint8_t* b, size_t n, uint64_t total, rbxcache::Entry& e)
    {
        if (n < 12 || memcmp(b, "RBXH", 4) != 0)
            return false;
        uint32_t urlLen = rd32(b + 8);
        if (urlLen > 4096 || n < 12 + (size_t)urlLen + 25)
            return false;
        const uint8_t* p = b + 12 + urlLen;
        uint32_t status = rd32(p + 1);
        uint32_t headLen = rd32(p + 5);
        e.bodyLen = rd32(p + 13);
        e.bodyHash = rd32(p + 17);
        uint64_t offset = 12ull + urlLen + 25 + headLen;
        if (status != 200 || offset + e.bodyLen != total)
            return false;
        e.bodyOffset = (uint32_t)offset;
        return true;
    }

    bool readFile(const std::wstring& path, std::string& out, size_t limit, uint64_t& total)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (1ll << 30);
        if (ok)
        {
            total = (uint64_t)size.QuadPart;
            size_t want = limit ? (size_t)std::min<uint64_t>(limit, total) : (size_t)total;
            out.resize(want);
            DWORD read = 0;
            ok = want == 0 || (ReadFile(h, out.data(), (DWORD)want, &read, nullptr) && read == want);
        }
        CloseHandle(h);
        return ok;
    }

    bool writeFileAtomic(const std::wstring& path, const std::string& data)
    {
        std::wstring tmp = path + L".rave";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
        CloseHandle(h);
        if (ok)
            ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok)
            DeleteFileW(tmp.c_str());
        return ok;
    }

    struct Statement
    {
        sqlite3_stmt* s = nullptr;
        Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
        ~Statement() { if (s) sqlite3_finalize(s); }
    };
}

uint32_t rbxcache::xxh32(const void* data, size_t len, uint32_t seed)
{
    const uint32_t p1 = 0x9E3779B1u, p2 = 0x85EBCA77u, p3 = 0xC2B2AE3Du, p4 = 0x27D4EB2Fu, p5 = 0x165667B1u;
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* end = p + len;
    uint32_t h;
    if (len >= 16)
    {
        uint32_t v1 = seed + p1 + p2, v2 = seed + p2, v3 = seed, v4 = seed - p1;
        const uint8_t* limit = end - 16;
        do
        {
            v1 = rotl(v1 + rd32(p) * p2, 13) * p1;
            v2 = rotl(v2 + rd32(p + 4) * p2, 13) * p1;
            v3 = rotl(v3 + rd32(p + 8) * p2, 13) * p1;
            v4 = rotl(v4 + rd32(p + 12) * p2, 13) * p1;
            p += 16;
        } while (p <= limit);
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    }
    else
    {
        h = seed + p5;
    }
    h += (uint32_t)len;
    while (p + 4 <= end)
    {
        h = rotl(h + rd32(p) * p3, 17) * p4;
        p += 4;
    }
    while (p < end)
    {
        h = rotl(h + (*p) * p5, 11) * p1;
        ++p;
    }
    h ^= h >> 15;
    h *= p2;
    h ^= h >> 13;
    h *= p3;
    h ^= h >> 16;
    return h;
}

uint32_t rbxcache::bodyHash(const void* data, size_t len)
{
    return len ? xxh32(data, len, kSeed) : 0;
}

std::wstring rbxcache::folder()
{
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return {};
    return std::wstring(buf) + L"\\Roblox";
}

bool rbxcache::exists()
{
    std::wstring dir = folder();
    return !dir.empty() && paths::exists(dir + L"\\rbx-storage.db");
}

rbxcache::Store::~Store()
{
    close();
}

bool rbxcache::Store::open(std::string& error)
{
    close();
    dir = folder();
    if (dir.empty() || !paths::exists(dir + L"\\rbx-storage.db"))
    {
        error = "The Roblox cache does not exist yet";
        return false;
    }
    std::string path = sys::narrow(dir + L"\\rbx-storage.db");
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
    {
        error = db ? sqlite3_errmsg(db) : "Could not open the Roblox cache";
        close();
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    Statement check(db, "SELECT id, content, size, category FROM files LIMIT 1");
    if (!check.s)
    {
        error = "The Roblox cache has an unknown format";
        close();
        return false;
    }
    return true;
}

void rbxcache::Store::close()
{
    if (db)
    {
        sqlite3_close_v2(db);
        db = nullptr;
    }
}

std::wstring rbxcache::Store::filePath(const std::string& id) const
{
    return dir + L"\\rbx-storage\\" + sys::widen(id.substr(0, 2)) + L"\\" + sys::widen(id);
}

bool rbxcache::Store::scan(std::vector<Entry>& out)
{
    out.clear();
    if (!db)
        return false;
    {
        Statement q(db, "SELECT id, content FROM files WHERE category = ?1 AND content IS NOT NULL");
        if (!q.s)
            return false;
        sqlite3_bind_int(q.s, 1, kContentCategory);
        while (sqlite3_step(q.s) == SQLITE_ROW)
        {
            if (sqlite3_column_bytes(q.s, 0) != 16)
                continue;
            Entry e;
            e.id = toHex(sqlite3_column_blob(q.s, 0), 16);
            const uint8_t* blob = (const uint8_t*)sqlite3_column_blob(q.s, 1);
            int n = sqlite3_column_bytes(q.s, 1);
            if (!blob || n <= 0)
                continue;
            e.inlined = true;
            e.size = (uint32_t)n;
            if (parseHeader(blob, (size_t)n, (uint64_t)n, e))
                out.push_back(e);
        }
    }
    {
        Statement q(db, "SELECT id FROM files WHERE category = ?1 AND content IS NULL");
        if (!q.s)
            return false;
        sqlite3_bind_int(q.s, 1, kContentCategory);
        std::string head;
        while (sqlite3_step(q.s) == SQLITE_ROW)
        {
            if (sqlite3_column_bytes(q.s, 0) != 16)
                continue;
            Entry e;
            e.id = toHex(sqlite3_column_blob(q.s, 0), 16);
            uint64_t total = 0;
            if (!readFile(filePath(e.id), head, 4096, total))
                continue;
            e.inlined = false;
            e.size = (uint32_t)total;
            if (parseHeader((const uint8_t*)head.data(), head.size(), total, e))
                out.push_back(e);
        }
    }
    return true;
}

bool rbxcache::Store::find(const std::string& id, Entry& out)
{
    uint8_t key[16];
    if (!db || !fromHex(id, key))
        return false;
    Statement q(db, "SELECT content FROM files WHERE id = ?1 AND category = ?2");
    if (!q.s)
        return false;
    sqlite3_bind_blob(q.s, 1, key, 16, SQLITE_TRANSIENT);
    sqlite3_bind_int(q.s, 2, kContentCategory);
    if (sqlite3_step(q.s) != SQLITE_ROW)
        return false;
    Entry e;
    e.id = id;
    if (sqlite3_column_type(q.s, 0) == SQLITE_NULL)
    {
        std::string head;
        uint64_t total = 0;
        if (!readFile(filePath(id), head, 4096, total))
            return false;
        e.inlined = false;
        e.size = (uint32_t)total;
        if (!parseHeader((const uint8_t*)head.data(), head.size(), total, e))
            return false;
    }
    else
    {
        const uint8_t* blob = (const uint8_t*)sqlite3_column_blob(q.s, 0);
        int n = sqlite3_column_bytes(q.s, 0);
        e.inlined = true;
        e.size = (uint32_t)std::max(n, 0);
        if (!blob || !parseHeader(blob, (size_t)n, (uint64_t)n, e))
            return false;
    }
    out = e;
    return true;
}

bool rbxcache::Store::readWhole(const std::string& id, bool inlined, std::string& out, size_t limit)
{
    if (!inlined)
    {
        uint64_t total = 0;
        return readFile(filePath(id), out, limit, total);
    }
    uint8_t key[16];
    if (!db || !fromHex(id, key))
        return false;
    Statement q(db, "SELECT content FROM files WHERE id = ?1");
    if (!q.s)
        return false;
    sqlite3_bind_blob(q.s, 1, key, 16, SQLITE_TRANSIENT);
    if (sqlite3_step(q.s) != SQLITE_ROW)
        return false;
    const char* blob = (const char*)sqlite3_column_blob(q.s, 0);
    int n = sqlite3_column_bytes(q.s, 0);
    if (!blob || n <= 0)
        return false;
    size_t take = limit ? std::min((size_t)n, limit) : (size_t)n;
    out.assign(blob, take);
    return true;
}

bool rbxcache::Store::readBody(const Entry& entry, std::string& body)
{
    std::string whole;
    if (!readWhole(entry.id, entry.inlined, whole, 0) || whole.size() != (size_t)entry.bodyOffset + entry.bodyLen)
        return false;
    body = whole.substr(entry.bodyOffset);
    return true;
}

bool rbxcache::Store::readHead(const Entry& entry, std::string& head, size_t bytes)
{
    std::string part;
    if (!readWhole(entry.id, entry.inlined, part, (size_t)entry.bodyOffset + bytes) || part.size() < entry.bodyOffset)
        return false;
    head = part.substr(entry.bodyOffset);
    return true;
}

bool rbxcache::Store::writeBody(Entry& entry, const std::string& body)
{
    uint8_t key[16];
    if (!db || body.empty() || !fromHex(entry.id, key))
        return false;
    std::string head;
    if (!readWhole(entry.id, entry.inlined, head, entry.bodyOffset) || head.size() != entry.bodyOffset)
        return false;
    uint32_t urlLen = rd32((const uint8_t*)head.data() + 8);
    if (12ull + urlLen + 25 > head.size())
        return false;
    uint8_t* fields = (uint8_t*)head.data() + 12 + urlLen;
    uint32_t hash = bodyHash(body.data(), body.size());
    wr32(fields + 13, (uint32_t)body.size());
    wr32(fields + 17, hash);
    std::string whole = head + body;

    bool inlineNow = whole.size() <= kInlineLimit;
    if (!inlineNow && !writeFileAtomic(filePath(entry.id), whole))
    {
        paths::ensureDir(dir + L"\\rbx-storage\\" + sys::widen(entry.id.substr(0, 2)));
        if (!writeFileAtomic(filePath(entry.id), whole))
            return false;
    }

    Statement q(db, inlineNow ? "UPDATE files SET content = ?1, size = ?2 WHERE id = ?3" : "UPDATE files SET content = NULL, size = ?2 WHERE id = ?3");
    if (!q.s)
        return false;
    if (inlineNow)
        sqlite3_bind_blob(q.s, 1, whole.data(), (int)whole.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.s, 2, (sqlite3_int64)whole.size());
    sqlite3_bind_blob(q.s, 3, key, 16, SQLITE_TRANSIENT);
    if (sqlite3_step(q.s) != SQLITE_DONE || sqlite3_changes(db) != 1)
        return false;
    if (inlineNow && !entry.inlined)
        DeleteFileW(filePath(entry.id).c_str());

    entry.inlined = inlineNow;
    entry.size = (uint32_t)whole.size();
    entry.bodyLen = (uint32_t)body.size();
    entry.bodyHash = hash;
    return true;
}

bool rbxcache::Store::remove(const std::string& id)
{
    uint8_t key[16];
    if (!db || !fromHex(id, key))
        return false;
    Statement q(db, "DELETE FROM files WHERE id = ?1");
    if (!q.s)
        return false;
    sqlite3_bind_blob(q.s, 1, key, 16, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(q.s) == SQLITE_DONE;
    DeleteFileW(filePath(id).c_str());
    return ok;
}
