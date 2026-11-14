#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace rbxcache
{
    struct Entry
    {
        std::string id;
        bool inlined = false;
        uint32_t size = 0;
        uint32_t bodyOffset = 0;
        uint32_t bodyLen = 0;
        uint32_t bodyHash = 0;
    };

    uint32_t xxh32(const void* data, size_t len, uint32_t seed);
    uint32_t bodyHash(const void* data, size_t len);

    std::wstring folder();
    bool exists();

    class Store
    {
    public:
        Store() = default;
        ~Store();
        Store(const Store&) = delete;
        Store& operator=(const Store&) = delete;

        bool open(std::string& error);
        void close();

        bool scan(std::vector<Entry>& out);
        bool readBody(const Entry& entry, std::string& body);
        bool readHead(const Entry& entry, std::string& head, size_t bytes);
        bool writeBody(Entry& entry, const std::string& body);
        bool remove(const std::string& id);
        bool find(const std::string& id, Entry& out);

    private:
        bool readWhole(const std::string& id, bool inlined, std::string& out, size_t limit);
        std::wstring filePath(const std::string& id) const;

        sqlite3* db = nullptr;
        std::wstring dir;
    };
}
