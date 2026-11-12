#include "zip.hpp"
#include "inflate.hpp"
#include "paths.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    uint32_t crc32(const uint8_t* data, size_t len)
    {
        static uint32_t table[256];
        static bool init = [] {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            return true;
        }();
        (void)init;

        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    bool safeName(const std::wstring& name)
    {
        if (name.empty() || name[0] == L'\\' || name.find(L':') != std::wstring::npos)
            return false;
        size_t start = 0;
        while (start <= name.size())
        {
            size_t stop = name.find(L'\\', start);
            if (stop == std::wstring::npos)
                stop = name.size();
            if (name.compare(start, stop - start, L"..") == 0)
                return false;
            start = stop + 1;
        }
        return true;
    }

    bool writeFile(const std::wstring& path, const uint8_t* data, size_t len)
    {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        bool ok = true;
        size_t done = 0;
        while (done < len)
        {
            DWORD chunk = (DWORD)std::min<size_t>(len - done, 1u << 24);
            DWORD written = 0;
            if (!WriteFile(h, data + done, chunk, &written, nullptr) || written != chunk)
            {
                ok = false;
                break;
            }
            done += written;
        }
        CloseHandle(h);
        return ok;
    }
}

bool zip::extract(const std::string& archive, const std::wstring& destDir, std::string& error)
{
    const uint8_t* base = (const uint8_t*)archive.data();
    size_t size = archive.size();
    if (size < 22)
    {
        error = "archive too small";
        return false;
    }

    size_t eocd = std::string::npos;
    size_t lowest = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (size_t i = size - 22 + 1; i-- > lowest;)
    {
        if (rd32(base + i) == 0x06054B50)
        {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos)
    {
        error = "end of central directory not found";
        return false;
    }

    uint16_t entries = rd16(base + eocd + 10);
    uint32_t cdSize = rd32(base + eocd + 12);
    uint32_t cdOffset = rd32(base + eocd + 16);
    if (entries == 0xFFFF || cdOffset == 0xFFFFFFFFu || (size_t)cdOffset + cdSize > size)
    {
        error = "unsupported or corrupt central directory";
        return false;
    }

    if (!paths::ensureDir(destDir))
    {
        error = "could not create folder";
        return false;
    }

    std::vector<uint8_t> buffer;
    size_t p = cdOffset;
    for (uint16_t i = 0; i < entries; ++i)
    {
        if (p + 46 > size || rd32(base + p) != 0x02014B50)
        {
            error = "corrupt central directory entry";
            return false;
        }

        uint16_t flags = rd16(base + p + 8);
        uint16_t method = rd16(base + p + 10);
        uint32_t crc = rd32(base + p + 16);
        uint32_t compSize = rd32(base + p + 20);
        uint32_t rawSize = rd32(base + p + 24);
        uint16_t nameLen = rd16(base + p + 28);
        uint16_t extraLen = rd16(base + p + 30);
        uint16_t commentLen = rd16(base + p + 32);
        uint32_t localOffset = rd32(base + p + 42);

        if (p + 46 + nameLen > size)
        {
            error = "corrupt entry name";
            return false;
        }
        std::string rawName((const char*)base + p + 46, nameLen);
        p += 46 + (size_t)nameLen + extraLen + commentLen;

        UINT cp = (flags & 0x800) ? CP_UTF8 : CP_OEMCP;
        int wlen = MultiByteToWideChar(cp, 0, rawName.data(), (int)rawName.size(), nullptr, 0);
        std::wstring name(wlen, L'\0');
        MultiByteToWideChar(cp, 0, rawName.data(), (int)rawName.size(), name.data(), wlen);
        for (auto& c : name)
            if (c == L'/')
                c = L'\\';
        while (!name.empty() && name[0] == L'\\')
            name.erase(0, 1);

        if (name.empty())
            continue;

        bool isDir = name.back() == L'\\';
        if (isDir)
            name.pop_back();
        if (name.empty())
            continue;

        if (!safeName(name))
        {
            error = "unsafe path in archive";
            return false;
        }

        std::wstring target = destDir + L"\\" + name;
        if (isDir)
        {
            if (!paths::ensureDir(target))
            {
                error = "could not create folder";
                return false;
            }
            continue;
        }

        if (compSize == 0xFFFFFFFFu || rawSize == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu)
        {
            error = "zip64 entries are not supported";
            return false;
        }
        if ((size_t)localOffset + 30 > size || rd32(base + localOffset) != 0x04034B50)
        {
            error = "corrupt local header";
            return false;
        }
        size_t dataOffset = (size_t)localOffset + 30 + rd16(base + localOffset + 26) + rd16(base + localOffset + 28);
        if (dataOffset + compSize > size)
        {
            error = "truncated entry";
            return false;
        }

        size_t slash = target.find_last_of(L'\\');
        if (slash != std::wstring::npos && !paths::ensureDir(target.substr(0, slash)))
        {
            error = "could not create folder";
            return false;
        }

        const uint8_t* src = base + dataOffset;
        const uint8_t* data = nullptr;
        if (method == 0)
        {
            if (compSize != rawSize)
            {
                error = "corrupt stored entry";
                return false;
            }
            data = src;
        }
        else if (method == 8)
        {
            buffer.resize(rawSize ? rawSize : 1);
            if (!inflate::raw(src, compSize, buffer.data(), rawSize))
            {
                error = "decompression failed";
                return false;
            }
            data = buffer.data();
        }
        else
        {
            error = "unsupported compression method";
            return false;
        }

        if (crc32(data, rawSize) != crc)
        {
            error = "checksum mismatch";
            return false;
        }
        if (!writeFile(target, data, rawSize))
        {
            error = "could not write file";
            return false;
        }
    }
    return true;
}
